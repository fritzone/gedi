#include "ProcessRunner.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <csignal>
#  include <fcntl.h>
#  include <poll.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
extern char** environ;
#endif

namespace proc {

namespace {

using Clock = std::chrono::steady_clock;

// After the command itself has ended, how long output is still read: what its
// descendants print right at the end arrives in that time, while one of them
// that lives on (holding the pipe) does not keep the build waiting.
constexpr auto kDrainAfterExit = std::chrono::milliseconds(300);
// A cancelled command gets this long to end on its own before it is killed.
constexpr auto kGraceAfterCancel = std::chrono::milliseconds(800);

// Splits a stream of bytes into lines for on_line; a "\r\n" counts as one break.
class LineSplitter {
public:
    explicit LineSplitter(const std::function<void(const std::string&)>& f) : f_(f) {}
    void feed(const char* data, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            if (data[i] == '\n') { flushLine(); continue; }
            pending_ += data[i];
        }
    }
    void finish() { if (!pending_.empty()) flushLine(); }
private:
    void flushLine() {
        if (!pending_.empty() && pending_.back() == '\r') pending_.pop_back();
        if (f_) f_(pending_);
        pending_.clear();
    }
    const std::function<void(const std::string&)>& f_;
    std::string pending_;
};

bool cancelRequested(const std::atomic<bool>* cancel) {
    return cancel && cancel->load();
}

} // namespace

#ifndef _WIN32
// ── POSIX ──────────────────────────────────────────────────────────────────────
// The command runs in a process group of its own, so a cancel reaches the shell
// and every tool it started (make → compiler → assembler) with one signal.
RunResult runShell(const std::string& command,
                   const std::function<void(const std::string&)>& on_line,
                   const std::atomic<bool>* cancel,
                   const std::map<std::string, std::string>& env)
{
    RunResult res;

    // Everything the child needs is prepared before fork(): after it, in a
    // threaded program, only async-signal-safe calls are allowed there.
    std::vector<std::string> env_strings;
    for (char** e = environ; e && *e; ++e) {
        std::string kv = *e;
        const std::string key = kv.substr(0, kv.find('='));
        if (!env.count(key)) env_strings.push_back(std::move(kv));
    }
    for (const auto& [k, v] : env) env_strings.push_back(k + "=" + v);
    std::vector<char*> envp;
    for (auto& s : env_strings) envp.push_back(s.data());
    envp.push_back(nullptr);
    std::string cmd = command;
    char sh[] = "/bin/sh", dash_c[] = "-c";
    char* argv[] = { sh, dash_c, cmd.data(), nullptr };

    int fds[2];
    if (pipe(fds) != 0) return res;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);

    const pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return res; }
    if (pid == 0) {
        setpgid(0, 0);
        const int null_in = open("/dev/null", O_RDONLY);
        if (null_in >= 0) { dup2(null_in, 0); close(null_in); }
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        execve("/bin/sh", argv, envp.data());
        _exit(127);
    }
    setpgid(pid, pid);          // also from here: no race with the kill below
    close(fds[1]);
    res.started = true;

    LineSplitter lines(on_line);
    bool exited = false, eof = false;
    int status = 0;
    Clock::time_point exited_at{}, cancelled_at{};
    bool killed = false;
    char buf[4096];

    while (!eof) {
        pollfd p{ fds[0], POLLIN, 0 };
        const int r = poll(&p, 1, 50);
        if (r > 0) {
            const ssize_t n = read(fds[0], buf, sizeof(buf));
            if (n > 0)      lines.feed(buf, (size_t)n);
            else if (n == 0) eof = true;
            else if (errno != EINTR && errno != EAGAIN) eof = true;
        }

        if (!res.cancelled && cancelRequested(cancel)) {
            res.cancelled = true;
            cancelled_at  = Clock::now();
            kill(-pid, SIGTERM);
        }
        if (res.cancelled && !killed && Clock::now() - cancelled_at > kGraceAfterCancel) {
            killed = true;
            kill(-pid, SIGKILL);
        }

        if (!exited) {
            const pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) { exited = true; exited_at = Clock::now(); }
        } else if (r == 0 && Clock::now() - exited_at > kDrainAfterExit) {
            break;              // ended; what is left holding the pipe is not ours to wait for
        }
    }
    close(fds[0]);
    if (!exited) {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
    lines.finish();

    if (WIFEXITED(status))        res.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) res.exit_code = 128 + WTERMSIG(status);
    return res;
}

#else
// ── Windows ────────────────────────────────────────────────────────────────────
// The command runs in a job object, so a cancel ends cmd.exe and every tool it
// started. CREATE_NO_WINDOW: no console window flashes up for it in gedi-gui.
RunResult runShell(const std::string& command,
                   const std::function<void(const std::string&)>& on_line,
                   const std::atomic<bool>* cancel,
                   const std::map<std::string, std::string>& env)
{
    RunResult res;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return res;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE null_in = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError  = wr;
    si.hStdInput  = (null_in == INVALID_HANDLE_VALUE) ? nullptr : null_in;

    // cmd.exe /s /c "<command>": /s strips exactly the outer pair of quotes we
    // add, so a command that itself starts with a quoted path arrives intact
    // (the same reason gedi_popen adds them, see platform_compat.h).
    const char* comspec = std::getenv("ComSpec");
    std::string cmdline = std::string("\"") + (comspec ? comspec : "cmd.exe") +
                          "\" /s /c \"" + command + "\"";
    std::vector<char> cmdline_buf(cmdline.begin(), cmdline.end());
    cmdline_buf.push_back('\0');

    // An environment block of our own only when something changes.
    std::vector<char> env_block;
    if (!env.empty()) {
        if (LPCH cur = GetEnvironmentStringsA()) {
            for (LPCH p = cur; *p; p += std::strlen(p) + 1) {
                std::string kv = p;
                const size_t eq = kv.find('=', 1);   // "=C:=C:\dir" entries start with '='
                std::string key = kv.substr(0, eq);
                bool replaced = false;
                for (const auto& [k, v] : env)
                    if (_stricmp(k.c_str(), key.c_str()) == 0) { replaced = true; break; }
                if (!replaced) {
                    env_block.insert(env_block.end(), kv.begin(), kv.end());
                    env_block.push_back('\0');
                }
            }
            FreeEnvironmentStringsA(cur);
        }
        for (const auto& [k, v] : env) {
            const std::string kv = k + "=" + v;
            env_block.insert(env_block.end(), kv.begin(), kv.end());
            env_block.push_back('\0');
        }
        env_block.push_back('\0');
    }

    HANDLE job = CreateJobObjectA(nullptr, nullptr);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessA(nullptr, cmdline_buf.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                   env_block.empty() ? nullptr : env_block.data(),
                                   nullptr, &si, &pi);
    CloseHandle(wr);
    if (null_in != INVALID_HANDLE_VALUE) CloseHandle(null_in);
    if (!ok) {
        CloseHandle(rd);
        if (job) CloseHandle(job);
        return res;
    }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    res.started = true;

    LineSplitter lines(on_line);
    bool exited = false;
    Clock::time_point exited_at{}, cancelled_at{};
    char buf[4096];

    while (true) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;   // all writers gone
        if (avail > 0) {
            DWORD n = 0;
            if (!ReadFile(rd, buf, std::min<DWORD>(avail, sizeof(buf)), &n, nullptr) || n == 0) break;
            lines.feed(buf, n);
        }

        if (!res.cancelled && cancelRequested(cancel)) {
            res.cancelled = true;
            cancelled_at  = Clock::now();
            if (job) TerminateJobObject(job, 1);
            else     TerminateProcess(pi.hProcess, 1);
        }

        if (!exited && WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            exited = true;
            exited_at = Clock::now();
        }
        if (avail == 0) {
            if (exited && Clock::now() - exited_at > kDrainAfterExit) break;
            if (res.cancelled && Clock::now() - cancelled_at > kGraceAfterCancel + kDrainAfterExit) break;
            Sleep(30);
        }
    }
    lines.finish();

    WaitForSingleObject(pi.hProcess, 2000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    res.exit_code = (int)code;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    if (job) CloseHandle(job);
    return res;
}
#endif

} // namespace proc
