#pragma once
#include "Debugger.h"

namespace dbg {

// Placeholder for the Windows / Visual Studio debugging backend. It exists so the
// architecture is explicitly multi-backend from day one; the real implementation
// (via the Windows debug engine, DbgEng, or a bundled MinGW/LLDB) lands later.
// Until then it reports "inactive" so the editor's debug UI simply stays disabled
// on Windows.
class MsvcDebugger : public IDebugger {
public:
    const char* name() const override { return "Visual Studio Debugger"; }

    bool load(const std::string&, const std::string&, const std::string&) override { return false; }
    void terminate() override {}
    bool isActive() const override { return false; }

    void run()      override {}
    void pause()    override {}
    void stepOver() override {}
    void stepInto() override {}
    void stepOut()  override {}
    void runToCursor(const std::string&, int) override {}

    void addBreakpoint(const std::string&, int)    override {}
    void removeBreakpoint(const std::string&, int) override {}

    std::vector<Frame>    backtrace() override { return {}; }
    std::vector<Variable> locals()    override { return {}; }
    std::string           evaluate(const std::string&) override { return ""; }

    bool pollEvent(Event&) override { return false; }
};

} // namespace dbg
