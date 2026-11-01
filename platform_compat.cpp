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

// ---- run a child program with redirected output, no console flash ---------------
int run_process_captured(const std::string& exe_path, const std::string& output_file) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE out = CreateFileA(output_file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) return -1;

    // gedi-gui is a Windows-subsystem process, so it has no stdin to hand down -
    // and because CREATE_NO_WINDOW still gives the child a console of its own,
    // a program that reads input would sit there forever waiting on a console
    // nobody can type into, with the WaitForSingleObject below never returning
    // and the editor frozen with it. When there is no stdin to pass on, hand the
    // child the null device instead: its reads then fail immediately with
    // end-of-file. A console build still passes its real stdin, so a program run
    // from the text-mode editor stays interactive.
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
