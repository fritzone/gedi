#include "GdbDebugger.h"

#include <cctype>
#include <cstring>
#include <sstream>

#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

namespace dbg {

// ─────────────────────────────────────────────────────────────────────────────
//  Minimal GDB/MI value parser
//
//  MI values are one of: a C-string ("...") , a tuple ({ k=v, ... }) or a list
//  ([ v, ... ] or [ k=v, ... ]). We parse into a generic Node tree and query it
//  by key. This is enough for the records the UI needs (frames, variables, …).
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct Node {
    std::string value;                                   // leaf (C-string) content
    std::vector<std::pair<std::string, Node>> children;  // tuple/list members

    const Node* get(const std::string& key) const {
        for (auto& c : children) if (c.first == key) return &c.second;
        return nullptr;
    }
    std::string str(const std::string& key) const {
        const Node* n = get(key);
        return n ? n->value : std::string();
    }
};

std::string unescape(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[++i];
            switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': out += '"';  break;
                case '\\': out += '\\'; break;
                default: out += c; break;
            }
        } else out += s[i];
    }
    return out;
}

// Forward decl.
Node parseValue(const std::string& s, size_t& i);

// Parse a comma-separated sequence of results ("name=value") or bare values until
// one of `closers` (or end). Fills node.children.
void parseMembers(const std::string& s, size_t& i, const char* closers, Node& node) {
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ',' || s[i] == ' ')) ++i;
        if (i >= s.size() || strchr(closers, s[i])) break;

        std::string name;
        // A named member starts with an identifier followed by '='.
        if (std::isalpha((unsigned char)s[i]) || s[i] == '_' || s[i] == '-') {
            size_t j = i;
            while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '-')) ++j;
            if (j < s.size() && s[j] == '=') { name = s.substr(i, j - i); i = j + 1; }
        }
        Node child = parseValue(s, i);
        node.children.emplace_back(std::move(name), std::move(child));
    }
}

Node parseValue(const std::string& s, size_t& i) {
    Node n;
    if (i >= s.size()) return n;
    if (s[i] == '"') {
        ++i; std::string raw;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) { raw += s[i]; raw += s[i + 1]; i += 2; }
            else raw += s[i++];
        }
        if (i < s.size()) ++i;   // closing quote
        n.value = unescape(raw);
    } else if (s[i] == '{') {
        ++i; parseMembers(s, i, "}", n);
        if (i < s.size() && s[i] == '}') ++i;
    } else if (s[i] == '[') {
        ++i; parseMembers(s, i, "]", n);
        if (i < s.size() && s[i] == ']') ++i;
    } else {
        // Bare token (rare) - read to next delimiter.
        size_t j = i;
        while (j < s.size() && s[j] != ',' && s[j] != '}' && s[j] != ']') ++j;
        n.value = s.substr(i, j - i);
        i = j;
    }
    return n;
}

// Parse the payload of a record ("reason=...,frame={...}") into a tuple Node.
Node parseRecord(const std::string& payload) {
    Node root; size_t i = 0;
    parseMembers(payload, i, "", root);
    return root;
}

StopReason reasonFrom(const std::string& r) {
    if (r == "breakpoint-hit" || r.rfind("watchpoint", 0) == 0) return StopReason::Breakpoint;
    if (r == "end-stepping-range" || r == "function-finished")   return StopReason::Step;
    if (r == "signal-received")                                  return StopReason::Signal;
    if (r.rfind("exited", 0) == 0)                               return StopReason::Exited;
    return StopReason::Other;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  GdbDebugger
// ─────────────────────────────────────────────────────────────────────────────

GdbDebugger::GdbDebugger() {}

GdbDebugger::~GdbDebugger() { terminate(); }

// Search PATH for an executable, the way the shell would. Used instead of
// shelling out to where/which so this stays usable from a GUI build with no
// console attached.
static std::string findOnPath(const std::string& stem) {
    const char* path = std::getenv("PATH");
    if (!path) return {};

#ifdef _WIN32
    const char sep = ';';
    const std::string exts[] = { ".exe", "" };
#else
    const char sep = ':';
    const std::string exts[] = { "" };
#endif

    std::error_code ec;
    std::string dir;
    std::stringstream ss(path);
    while (std::getline(ss, dir, sep)) {
        if (dir.empty()) continue;
        for (const std::string& ext : exts) {
            const std::filesystem::path p = std::filesystem::path(dir) / (stem + ext);
            if (std::filesystem::is_regular_file(p, ec)) return p.string();
        }
    }
    return {};
}

// gdb first where it exists, then lldb-mi. The latter is a GDB/MI front end on
// top of LLDB, so the whole protocol layer below applies unchanged - which is
// what makes the Windows build debuggable at all, since the bundled toolchain
// ships LLDB and no gdb.
std::string GdbDebugger::findDebugger() {
    for (const char* candidate : { "gdb", "lldb-mi" }) {
        const std::string found = findOnPath(candidate);
        if (!found.empty()) return found;
    }
    return {};
}

bool GdbDebugger::spawnDebugger() {
    m_program = findDebugger();
    if (m_program.empty()) return false;
    m_is_lldb = m_program.find("lldb") != std::string::npos;

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof sa;
    sa.bInheritHandle = TRUE;

    HANDLE in_r = nullptr, in_w = nullptr, out_r = nullptr, out_w = nullptr;
    if (!CreatePipe(&in_r,  &in_w,  &sa, 0)) return false;
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) { CloseHandle(in_r); CloseHandle(in_w); return false; }

    // Only the child's ends may be inherited; ours must not be, or the pipe
    // never reports EOF when the child exits.
    SetHandleInformation(in_w,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb         = sizeof si;
    si.dwFlags    = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;               // no console flash from the GUI build
    si.hStdInput  = in_r;
    si.hStdOutput = out_w;
    si.hStdError  = out_w;

    // lldb-mi takes the program as a bare argument; gdb needs its MI mode asked
    // for explicitly and its init files suppressed.
    std::string cmd = "\"" + m_program + "\"";
    if (!m_is_lldb) cmd += " --interpreter=mi2 -q --nx";
    if (!m_exe_path.empty()) cmd += " \"" + m_exe_path + "\"";

    PROCESS_INFORMATION pi{};
    std::vector<char> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back('\0');

    const BOOL ok = CreateProcessA(nullptr, mutable_cmd.data(), nullptr, nullptr,
                                   TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(in_r);
    CloseHandle(out_w);
    if (!ok) { CloseHandle(in_w); CloseHandle(out_r); return false; }

    CloseHandle(pi.hThread);
    m_proc  = pi.hProcess;
    m_in_w  = in_w;
    m_out_r = out_r;
    return true;
#else
    int in_pipe[2], out_pipe[2];              // in: parent→debugger stdin; out: back
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) return false;

    m_pid = (long)fork();
    if (m_pid < 0) return false;

    if (m_pid == 0) {                         // ── child ──
        dup2(in_pipe[0],  STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        if (m_is_lldb)
            execlp(m_program.c_str(), m_program.c_str(), m_exe_path.c_str(), (char*)nullptr);
        else
            execlp(m_program.c_str(), m_program.c_str(), "--interpreter=mi2", "-q", "--nx",
                   m_exe_path.c_str(), (char*)nullptr);
        _exit(127);                           // exec failed
    }

    // ── parent ──
    close(in_pipe[0]);
    close(out_pipe[1]);
    m_to_gdb   = in_pipe[1];
    m_from_gdb = out_pipe[0];
    return true;
#endif
}

// ---- platform I/O ---------------------------------------------------------
bool GdbDebugger::ioWrite(const std::string& text) {
#ifdef _WIN32
    if (!m_in_w) return false;
    DWORD written = 0;
    return WriteFile(m_in_w, text.data(), (DWORD)text.size(), &written, nullptr) != 0;
#else
    if (m_to_gdb < 0) return false;
    ssize_t w = write(m_to_gdb, text.data(), text.size());
    return w == (ssize_t)text.size();
#endif
}

int GdbDebugger::ioRead(char* buf, size_t len) {
#ifdef _WIN32
    if (!m_out_r) return 0;
    DWORD got = 0;
    if (!ReadFile(m_out_r, buf, (DWORD)len, &got, nullptr)) return 0;
    return (int)got;
#else
    if (m_from_gdb < 0) return 0;
    return (int)read(m_from_gdb, buf, len);
#endif
}

void GdbDebugger::ioShutdown() {
#ifdef _WIN32
    if (m_proc) {
        // Give it a moment to act on -gdb-exit before insisting.
        if (WaitForSingleObject(m_proc, 500) == WAIT_TIMEOUT) TerminateProcess(m_proc, 0);
    }
    if (m_in_w)  { CloseHandle(m_in_w);  m_in_w  = nullptr; }
    if (m_out_r) { CloseHandle(m_out_r); m_out_r = nullptr; }
    if (m_proc)  { CloseHandle(m_proc);  m_proc  = nullptr; }
#else
    if (m_pid > 0) kill((pid_t)m_pid, SIGTERM);
    if (m_from_gdb >= 0) close(m_from_gdb);
    if (m_to_gdb   >= 0) close(m_to_gdb);
    m_from_gdb = m_to_gdb = -1;
#endif
}

bool GdbDebugger::load(const std::string& exe_path, const std::string& args,
                       const std::string& working_dir) {
    if (m_active.load()) terminate();
    m_exe_path = exe_path;
    if (!spawnDebugger()) return false;

    m_active.store(true);
    m_running.store(false);
    m_started = false;
    m_reader = std::thread([this] { readerLoop(); });

    bool ok = true;
    if (!working_dir.empty()) sendAndWait("-environment-cd " + working_dir, ok);
    if (!args.empty())        sendAndWait("-exec-arguments " + args, ok);
    return true;
}

void GdbDebugger::terminate() {
    if (!m_active.exchange(false)) return;
    // Ask the debugger to quit, then tear the pipes down so the reader unblocks.
    ioWrite("-gdb-exit\n");
    ioShutdown();
    m_cv.notify_all();
    if (m_reader.joinable()) m_reader.join();
#ifndef _WIN32
    // Reap the child so it does not linger as a zombie. ioShutdown already
    // closed the Windows handles, which is the equivalent there.
    if (m_pid > 0) { int st = 0; waitpid((pid_t)m_pid, &st, 0); m_pid = -1; }
#endif
}

void GdbDebugger::readerLoop() {
    std::string buf;
    char chunk[4096];
    while (m_active.load()) {
        const int n = ioRead(chunk, sizeof(chunk));
        if (n <= 0) break;
        buf.append(chunk, (size_t)n);
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            handleLine(line);
        }
    }
    // Reader exiting → the session is over.
    if (m_active.load()) {
        Event e; e.type = Event::Exited; e.exitCode = 0;
        pushEvent(e);
        m_active.store(false);
    }
    m_cv.notify_all();
}

int GdbDebugger::send(const std::string& cmd) {
    int token = ++m_token;
    ioWrite(std::to_string(token) + cmd + "\n");
    return token;
}

std::string GdbDebugger::sendAndWait(const std::string& cmd, bool& ok, int timeout_ms) {
    int token;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        token = ++m_token;
        m_results[token] = Result{};
    }
    ioWrite(std::to_string(token) + cmd + "\n");

    std::unique_lock<std::mutex> lk(m_mutex);
    bool got = m_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                             [&] { return m_results[token].done || !m_active.load(); });
    Result r = m_results[token];
    m_results.erase(token);
    ok = got && r.ok;
    return r.payload;
}

void GdbDebugger::pushEvent(const Event& e) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_events.push_back(e);
}

void GdbDebugger::handleLine(const std::string& line) {
    if (line.empty()) return;

    // Optional leading numeric token.
    size_t i = 0;
    int token = 0;
    while (i < line.size() && std::isdigit((unsigned char)line[i]))
        token = token * 10 + (line[i++] - '0');
    if (i >= line.size()) return;

    char kind = line[i];
    std::string rest = line.substr(i + 1);

    switch (kind) {
        case '^': {   // result record: "class,payload"
            size_t comma = rest.find(',');
            std::string cls = rest.substr(0, comma);
            std::string payload = (comma == std::string::npos) ? "" : rest.substr(comma + 1);
            std::lock_guard<std::mutex> lk(m_mutex);
            auto it = m_results.find(token);
            if (it != m_results.end()) {
                it->second.done = true;
                it->second.ok   = (cls != "error");
                it->second.payload = payload;
            }
            m_cv.notify_all();
            break;
        }
        case '*': {   // exec async: "class,payload"
            size_t comma = rest.find(',');
            std::string cls = rest.substr(0, comma);
            std::string payload = (comma == std::string::npos) ? "" : rest.substr(comma + 1);
            if (cls == "running") {
                m_running.store(true);
                Event e; e.type = Event::Running; pushEvent(e);
            } else if (cls == "stopped") {
                m_running.store(false);
                Node rec = parseRecord(payload);
                std::string reason = rec.str("reason");
                if (reason.rfind("exited", 0) == 0) {
                    Event e; e.type = Event::Exited;
                    std::string code = rec.str("exit-code");
                    e.exitCode = code.empty() ? 0 : (int)strtol(code.c_str(), nullptr, 0);
                    pushEvent(e);
                } else {
                    Event e; e.type = Event::Stopped;
                    e.reason = reasonFrom(reason);
                    if (const Node* fr = rec.get("frame")) {
                        e.file = fr->str("fullname");
                        if (e.file.empty()) e.file = fr->str("file");
                        std::string ln = fr->str("line");
                        e.line = ln.empty() ? 0 : (int)strtol(ln.c_str(), nullptr, 10);
                        e.func = fr->str("func");
                    }
                    pushEvent(e);
                }
            }
            break;
        }
        case '~':   // console stream
        case '@': { // target (inferior) stream
            // rest is a C-string; strip the surrounding quotes and unescape.
            size_t q0 = rest.find('"');
            size_t q1 = rest.rfind('"');
            std::string text = (q0 != std::string::npos && q1 > q0)
                             ? unescape(rest.substr(q0 + 1, q1 - q0 - 1)) : rest;
            Event e; e.type = Event::Output; e.text = text; pushEvent(e);
            break;
        }
        case '=':   // notify async (breakpoint-created, thread events, …) - ignore
        case '&':   // log stream - ignore
        case '(':   // "(gdb)" prompt - ignore
            break;
        default: {
            // A line that is not an MI record: inferior stdout/stderr.
            Event e; e.type = Event::Output; e.text = line + "\n"; pushEvent(e);
            break;
        }
    }
}

void GdbDebugger::run() {
    if (!m_active.load()) return;
    bool ok = true;
    if (!m_started) { m_started = true; sendAndWait("-exec-run", ok, 1500); }
    else            { send("-exec-continue"); }
}
void GdbDebugger::pause() {
#ifdef _WIN32
    // No SIGINT to send: MI's own interrupt is the portable way, and it is what
    // lldb-mi understands in any case.
    if (m_active.load()) send("-exec-interrupt");
#else
    if (m_pid > 0) kill((pid_t)m_pid, SIGINT);
#endif
}
void GdbDebugger::stepOver() { if (m_active.load()) send("-exec-next"); }
void GdbDebugger::stepInto() { if (m_active.load()) send("-exec-step"); }
void GdbDebugger::stepOut()  { if (m_active.load()) send("-exec-finish"); }

void GdbDebugger::runToCursor(const std::string& file, int line) {
    if (!m_active.load()) return;
    bool ok = true;
    // A one-shot breakpoint, then start or resume until it's hit.
    sendAndWait("-break-insert -t " + file + ":" + std::to_string(line), ok);
    if (!m_started) { m_started = true; send("-exec-run"); }
    else            { send("-exec-continue"); }
}

void GdbDebugger::addBreakpoint(const std::string& file, int line) {
    if (!m_active.load()) return;
    bool ok = true;
    std::string payload = sendAndWait("-break-insert " + file + ":" + std::to_string(line), ok);
    if (ok) {
        Node rec = parseRecord(payload);
        if (const Node* b = rec.get("bkpt")) {
            std::string ids = b->str("number");
            int id = ids.empty() ? 0 : (int)strtol(ids.c_str(), nullptr, 10);
            std::lock_guard<std::mutex> lk(m_mutex);
            m_breakpoints.push_back({ id, file, line });
        }
    }
}

void GdbDebugger::removeBreakpoint(const std::string& file, int line) {
    if (!m_active.load()) return;
    int id = -1;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (size_t k = 0; k < m_breakpoints.size(); ++k)
            if (m_breakpoints[k].file == file && m_breakpoints[k].line == line) {
                id = m_breakpoints[k].id;
                m_breakpoints.erase(m_breakpoints.begin() + k);
                break;
            }
    }
    if (id >= 0) send("-break-delete " + std::to_string(id));
}

std::vector<Frame> GdbDebugger::backtrace() {
    std::vector<Frame> out;
    if (!m_active.load()) return out;
    bool ok = true;
    std::string payload = sendAndWait("-stack-list-frames", ok);
    if (!ok) return out;
    Node rec = parseRecord(payload);
    if (const Node* stack = rec.get("stack")) {
        for (auto& c : stack->children) {
            const Node& f = c.second;
            Frame fr;
            fr.level = (int)strtol(f.str("level").c_str(), nullptr, 10);
            fr.func  = f.str("func");
            fr.file  = f.str("fullname"); if (fr.file.empty()) fr.file = f.str("file");
            fr.line  = (int)strtol(f.str("line").c_str(), nullptr, 10);
            fr.addr  = f.str("addr");
            out.push_back(std::move(fr));
        }
    }
    return out;
}

std::vector<Variable> GdbDebugger::locals() {
    std::vector<Variable> out;
    if (!m_active.load()) return out;
    bool ok = true;
    std::string payload = sendAndWait("-stack-list-variables --simple-values", ok);
    if (!ok) return out;
    Node rec = parseRecord(payload);
    if (const Node* vars = rec.get("variables")) {
        for (auto& c : vars->children) {
            const Node& v = c.second;
            Variable var;
            var.name  = v.str("name");
            var.type  = v.str("type");
            var.value = v.str("value");
            out.push_back(std::move(var));
        }
    }
    return out;
}

std::string GdbDebugger::evaluate(const std::string& expr) {
    if (!m_active.load()) return "";
    bool ok = true;
    std::string payload = sendAndWait("-data-evaluate-expression \"" + expr + "\"", ok);
    if (!ok) return "";
    return parseRecord(payload).str("value");
}


bool GdbDebugger::pollEvent(Event& out) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_events.empty()) return false;
    out = m_events.front();
    m_events.pop_front();
    return true;
}

} // namespace dbg
