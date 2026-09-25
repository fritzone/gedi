#pragma once
#include <string>
#include <vector>
#include <memory>

// Platform-agnostic debugger abstraction.
//
// The editor talks only to IDebugger; concrete backends implement it:
//   * GdbDebugger  - GDB/MI on Linux/macOS (and MinGW GDB on Windows).
//   * MsvcDebugger - the Visual Studio / Windows debugging engine (future).
//
// createDebugger() picks the right backend for the current platform/toolchain.
namespace dbg {

// Why execution stopped (mirrors the subset of reasons the UI cares about).
enum class StopReason { Breakpoint, Step, Paused, Entry, Signal, Exited, Error, Other };

// A stack frame reported by the backend when stopped.
struct Frame {
    int         level = 0;
    std::string func;
    std::string file;    // full path when the backend knows it
    int         line = 0;
    std::string addr;
};

// A variable (local, argument or watch result).
struct Variable {
    std::string name;
    std::string value;
    std::string type;
};

// A source breakpoint. `id` is assigned by the backend once bound.
struct Breakpoint {
    int         id = 0;
    std::string file;
    int         line = 0;
    bool        verified = false;
};

// An event pushed from the backend to the editor. The editor drains these from
// its main loop via IDebugger::pollEvent(), so all UI updates stay single-threaded.
struct Event {
    enum Type { Stopped, Running, Exited, Output, BreakpointChanged, Error } type = Output;

    // Stopped:
    StopReason  reason = StopReason::Other;
    std::string file;      // full path of the current source, if known
    int         line = 0;
    std::string func;

    // Exited:
    int         exitCode = 0;

    // Output / Error: program or debugger text.
    std::string text;
};

// The backend interface. All methods are called from the editor's main thread;
// backends run their own I/O threads internally and surface results via pollEvent.
class IDebugger {
public:
    virtual ~IDebugger() = default;

    // Human-readable backend name ("GDB", "Visual Studio Debugger", ...).
    virtual const char* name() const = 0;

    // Load an executable for debugging (does not start it). working_dir may be
    // empty. Returns false if the backend/tool could not be started.
    virtual bool load(const std::string& exe_path,
                      const std::string& args,
                      const std::string& working_dir) = 0;

    // Tear the session down (kills the inferior and the backend process).
    virtual void terminate() = 0;

    // True while a debug session is alive.
    virtual bool isActive() const = 0;

    // Execution control. run() starts the inferior the first time and continues
    // afterwards. The resulting stop/exit arrives asynchronously via pollEvent.
    virtual void run()      = 0;   // start or continue
    virtual void pause()    = 0;
    virtual void stepOver() = 0;
    virtual void stepInto() = 0;
    virtual void stepOut()  = 0;
    // Run (or start) until execution reaches file:line, then stop ("go to cursor").
    virtual void runToCursor(const std::string& file, int line) = 0;

    // Breakpoints. May be set before load()/run() or while stopped.
    virtual void addBreakpoint(const std::string& file, int line)    = 0;
    virtual void removeBreakpoint(const std::string& file, int line) = 0;

    // State queries - only meaningful while stopped. These block briefly on the
    // backend, so call them in response to a Stopped event, not every frame.
    virtual std::vector<Frame>    backtrace() = 0;
    virtual std::vector<Variable> locals()    = 0;
    virtual std::string           evaluate(const std::string& expr) = 0;

    // Drain one queued event. Returns false when the queue is empty.
    virtual bool pollEvent(Event& out) = 0;
};

// Construct the debugger backend appropriate for this build/platform. Returns
// null when no backend is available (e.g. gdb not installed).
std::unique_ptr<IDebugger> createDebugger();

} // namespace dbg
