#pragma once
#include "Debugger.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dbg {

// GDB/MI2 backend. Spawns `gdb --interpreter=mi2` as a child process, talks the
// MI protocol over pipes, and translates MI async records into dbg::Event.
class GdbDebugger : public IDebugger {
public:
    GdbDebugger();
    ~GdbDebugger() override;

    const char* name() const override { return m_is_lldb ? "LLDB" : "GDB"; }

    bool load(const std::string& exe_path, const std::string& args,
              const std::string& working_dir) override;
    void terminate() override;
    bool isActive() const override { return m_active.load(); }

    void run()      override;
    void pause()    override;
    void stepOver() override;
    void stepInto() override;
    void stepOut()  override;
    void runToCursor(const std::string& file, int line) override;

    void addBreakpoint(const std::string& file, int line)    override;
    void removeBreakpoint(const std::string& file, int line) override;

    std::vector<Frame>    backtrace() override;
    std::vector<Variable> locals()    override;
    std::string           evaluate(const std::string& expr) override;

    bool pollEvent(Event& out) override;

private:
    // ---- process ----
    // Locate the MI debugger to drive: gdb where the machine has one, otherwise
    // lldb-mi, which speaks the same protocol on top of LLDB. Empty when
    // neither can be found.
    static std::string findDebugger();

    bool spawnDebugger();
    void readerLoop();               // background thread: read + parse MI
    void handleLine(const std::string& line);

    // ---- platform I/O ----
    // The only part of this backend that differs between POSIX and Windows;
    // everything above the pipe is plain MI, and shared.
    bool ioWrite(const std::string& text);
    int  ioRead(char* buf, size_t len);   // <= 0 means the pipe closed
    void ioShutdown();                    // close the pipes, stop the process

    // ---- MI command I/O ----
    // Fire-and-forget: writes "token-cmd\n".
    int  send(const std::string& cmd);
    // Send and wait for the matching result record ("^done,..."/"^error,...").
    // Returns the payload after the result class; sets ok. Times out safely.
    std::string sendAndWait(const std::string& cmd, bool& ok, int timeout_ms = 4000);

    void pushEvent(const Event& e);

    // ---- state ----
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_running{false};   // inferior currently executing
    bool              m_started = false;  // inferior launched at least once

    // Which program is actually behind this session.
    std::string m_program;
    bool        m_is_lldb = false;

#ifdef _WIN32
    void* m_in_w  = nullptr;   // HANDLE: our end of the debugger's stdin
    void* m_out_r = nullptr;   // HANDLE: our end of its stdout
    void* m_proc  = nullptr;   // HANDLE: the debugger process
#else
    int  m_to_gdb   = -1;   // write end of the debugger's stdin
    int  m_from_gdb = -1;   // read end of its stdout
    long m_pid      = -1;   // debugger process id (pid_t)
#endif

    std::thread       m_reader;
    std::atomic<int>  m_token{0};

    std::mutex              m_mutex;
    std::condition_variable m_cv;

    // Pending synchronous results, keyed by command token.
    struct Result { bool done = false; bool ok = false; std::string payload; };
    std::map<int, Result> m_results;

    // Event queue drained by the editor.
    std::deque<Event> m_events;

    // Breakpoints we've set, so removeBreakpoint can map file:line → gdb id.
    struct Bp { int id; std::string file; int line; };
    std::vector<Bp> m_breakpoints;

    std::string m_exe_path;
};

} // namespace dbg
