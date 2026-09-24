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

    const char* name() const override { return "GDB"; }

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
    bool spawnGdb();
    void readerLoop();               // background thread: read + parse MI
    void handleLine(const std::string& line);

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

    int  m_to_gdb   = -1;   // write end of gdb's stdin
    int  m_from_gdb = -1;   // read end of gdb's stdout
    long m_pid      = -1;   // gdb process id (pid_t)

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
