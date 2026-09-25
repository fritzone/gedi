#include "Debugger.h"
#include "GdbDebugger.h"
#include "MsvcDebugger.h"

namespace dbg {

// The GDB/MI backend everywhere, because it is no longer GDB-specific: it picks
// up gdb where the machine has one and lldb-mi otherwise, and lldb-mi speaks the
// same protocol on top of LLDB. That covers Windows, where the bundled toolchain
// ships LLDB and no gdb - previously this returned the MsvcDebugger stub and the
// debugger pane had nothing behind it at all.
//
// load() reports false if no debugger could be launched, so a null return here
// would specifically mean "no backend compiled in".
std::unique_ptr<IDebugger> createDebugger() {
    return std::make_unique<GdbDebugger>();
}

} // namespace dbg
