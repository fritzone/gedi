// platform_compat.cpp - Windows-only implementations declared in platform_compat.h.
// Compiled as a no-op translation unit on every other platform.
#include "platform_compat.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <fcntl.h>
#include <io.h>
#include <share.h>

// ---- dirent.h emulation -------------------------------------------------------
struct DIR {
    HANDLE          handle = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAA data{};
    bool            first  = true;
    dirent          entry{};
};

DIR* opendir(const char* path) {
    std::string pattern = std::string(path) + "\\*";
    auto* d = new DIR();
    d->handle = FindFirstFileA(pattern.c_str(), &d->data);
    if (d->handle == INVALID_HANDLE_VALUE) {
        delete d;
        return nullptr;
    }
    d->first = true;
    return d;
}

dirent* readdir(DIR* d) {
    if (!d) return nullptr;
    if (!d->first) {
        if (!FindNextFileA(d->handle, &d->data))
            return nullptr;
    }
    d->first = false;
    strncpy(d->entry.d_name, d->data.cFileName, PATH_MAX - 1);
    d->entry.d_name[PATH_MAX - 1] = '\0';
    return &d->entry;
}

int closedir(DIR* d) {
    if (!d) return -1;
    if (d->handle != INVALID_HANDLE_VALUE) FindClose(d->handle);
    delete d;
    return 0;
}

// ---- fnmatch: minimal '*'/'?' glob, case-insensitive --------------------------
static bool fnmatch_impl(const char* pat, const char* str) {
    while (*pat) {
        if (*pat == '*') {
            while (*pat == '*') ++pat;
            if (!*pat) return true;
            for (const char* s = str; *s; ++s)
                if (fnmatch_impl(pat, s)) return true;
            return false;
        } else if (*pat == '?') {
            if (!*str) return false;
            ++pat; ++str;
        } else {
            if (!*str || std::tolower((unsigned char)*pat) != std::tolower((unsigned char)*str))
                return false;
            ++pat; ++str;
        }
    }
    return !*str;
}

int fnmatch(const char* pattern, const char* str, int /*flags*/) {
    return fnmatch_impl(pattern, str) ? 0 : 1;
}

// ---- mkstemp -------------------------------------------------------------------
int mkstemp(char* tmpl) {
    if (_mktemp_s(tmpl, strlen(tmpl) + 1) != 0)
        return -1;
    int fd = -1;
    if (_sopen_s(&fd, tmpl, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                 _SH_DENYNO, _S_IREAD | _S_IWRITE) != 0)
        return -1;
    return fd;
}

// ---- current executable path ----------------------------------------------------
long win_self_exe_path(char* buf, size_t bufsize) {
    DWORD len = GetModuleFileNameA(nullptr, buf, (DWORD)bufsize);
    if (len == 0 || len >= bufsize) return -1;
    return (long)len;
}

// ---- the no-window shell (popen / pclose / shell) ------------------------------
// See platform_compat.h for why none of this goes through _popen or system().
namespace {

// A child started by gedi_popen, found again by the FILE* the caller holds -
// that stream is the only handle on the child it is given.
std::mutex              g_child_mutex;
std::map<FILE*, HANDLE> g_children;

// The command interpreter: what the environment says, else the one in the
// system directory. Both are absolute, which is what lets spawnShell() name the
// program explicitly instead of letting a PATH entry decide what "cmd.exe" is.
std::string shellPath() {
    char buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("COMSPEC", buf, (DWORD)sizeof(buf));
    if (n > 0 && n < sizeof(buf)) return buf;
    n = GetSystemDirectoryA(buf, (DWORD)sizeof(buf));
    if (n > 0 && n < sizeof(buf)) return std::string(buf) + "\\cmd.exe";
    return "cmd.exe";
}

// The null device, inheritable so it can be handed to a child.
HANDLE openNull(SECURITY_ATTRIBUTES& sa) {
    return CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

// Start `<comspec> /S /C "<command>"` with the three standard handles given and
// no console window. Returns the process handle, or nullptr if it would not
// start.
//
// CREATE_NO_WINDOW, and specifically not DETACHED_PROCESS: the child does get a
// console, it is simply one with no window. Detaching it instead sounds safer
// and is worse - a console program handed no console allocates one for itself
// the moment it wants one, and cmd.exe does, so the command ends up with a
// brand new console that the default terminal application duly puts on screen.
// Giving it a windowless console up front is what leaves it nothing to ask for.
HANDLE spawnShell(const std::string& command, HANDLE in, HANDLE out, HANDLE err) {
    const std::string app     = shellPath();
    const std::string cmdline = "\"" + app + "\" /S /C \"" + command + "\"";

    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = in;
    si.hStdOutput = out;
    si.hStdError  = err;

    std::vector<char> buf(cmdline.begin(), cmdline.end());
    buf.push_back('\0');

    // Only the degenerate fallback in shellPath() is a bare name; let
    // CreateProcess resolve that one from PATH rather than fail outright.
    const bool absolute = app.find('\\') != std::string::npos ||
                          app.find('/')  != std::string::npos;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(absolute ? app.c_str() : nullptr, buf.data(), nullptr, nullptr,
                        /*bInheritHandles=*/TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi))
        return nullptr;
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

} // namespace

FILE* gedi_popen(const std::string& command, const char* mode) {
    const bool writing = mode && std::strchr(mode, 'w') != nullptr;
    const bool binary  = mode && std::strchr(mode, 'b') != nullptr;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return nullptr;

    // The end we keep must not travel to the child: while the child holds a
    // copy of the write end, a pipe we are reading never reports end-of-file.
    HANDLE ours   = writing ? wr : rd;
    HANDLE theirs = writing ? rd : wr;
    SetHandleInformation(ours, HANDLE_FLAG_INHERIT, 0);

    const HANDLE nul   = openNull(sa);
    const HANDLE nul_h = (nul == INVALID_HANDLE_VALUE) ? nullptr : nul;

    HANDLE proc = writing ? spawnShell(command, theirs, nul_h, nul_h)
                          : spawnShell(command, nul_h, theirs, nul_h);

    CloseHandle(theirs);        // the child must hold the only copy
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!proc) { CloseHandle(ours); return nullptr; }

    // From here the handle belongs to the fd, and then to the FILE*: closing
    // either one closes it too, so it must not be closed by hand again.
    const int fd = _open_osfhandle((intptr_t)ours,
                                   (writing ? _O_WRONLY : _O_RDONLY) |
                                   (binary  ? _O_BINARY : _O_TEXT));
    if (fd == -1) {
        CloseHandle(ours);
        CloseHandle(proc);
        return nullptr;
    }

    FILE* f = _fdopen(fd, writing ? (binary ? "wb" : "w")
                                  : (binary ? "rb" : "r"));
    if (!f) {
        _close(fd);
        CloseHandle(proc);
        return nullptr;
    }

    std::lock_guard<std::mutex> lk(g_child_mutex);
    g_children[f] = proc;
    return f;
}

int gedi_pclose(FILE* stream) {
    if (!stream) return -1;

    HANDLE proc = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_child_mutex);
        auto it = g_children.find(stream);
        if (it != g_children.end()) {
            proc = it->second;
            g_children.erase(it);
        }
    }

    // Closed before the wait, not after: a child reading from the pipe only
    // sees end-of-file - and only then exits - once our end of it is gone.
    fclose(stream);
    if (!proc) return -1;

    WaitForSingleObject(proc, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(proc, &code);
    CloseHandle(proc);
    return (int)code;
}

int gedi_shell(const std::string& command) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    const HANDLE nul   = openNull(sa);
    const HANDLE nul_h = (nul == INVALID_HANDLE_VALUE) ? nullptr : nul;

    HANDLE proc = spawnShell(command, nul_h, nul_h, nul_h);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!proc) return -1;

    WaitForSingleObject(proc, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(proc, &code);
    CloseHandle(proc);
    return (int)code;
}

// ---- run a child program with redirected output, no console flash ---------------
int run_process_captured(const std::string& exe_path, const std::string& output_file) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE out = CreateFileA(output_file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) return -1;

    // gedi-gui is a Windows-subsystem process, so it has no stdin to hand down.
    // A program that reads input would otherwise sit there forever, with the
    // WaitForSingleObject below never returning and the editor frozen with it.
    // When there is no stdin to pass on, hand the child the null device instead:
    // its reads then fail immediately with end-of-file. A console build still
    // passes its real stdin, so a program run from the text-mode editor stays
    // interactive.
    HANDLE in      = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE null_in = INVALID_HANDLE_VALUE;
    if (in == nullptr || in == INVALID_HANDLE_VALUE) {
        null_in = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        in = (null_in == INVALID_HANDLE_VALUE) ? nullptr : null_in;
    }

    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = out;
    si.hStdError  = out;
    si.hStdInput  = in;

    PROCESS_INFORMATION pi{};
    std::string cmdline = "\"" + exe_path + "\"";
    std::vector<char> cmdline_buf(cmdline.begin(), cmdline.end());
    cmdline_buf.push_back('\0');

    BOOL ok = CreateProcessA(nullptr, cmdline_buf.data(), nullptr, nullptr,
                              /*bInheritHandles=*/TRUE, CREATE_NO_WINDOW,
                              nullptr, nullptr, &si, &pi);
    CloseHandle(out);
    if (null_in != INVALID_HANDLE_VALUE) CloseHandle(null_in);
    if (!ok) return -1;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
}

#endif // _WIN32
