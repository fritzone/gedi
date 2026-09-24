#include "Debugger.h"
#include "GdbDebugger.h"
#include "MsvcDebugger.h"

namespace dbg {

// Choose the backend for this platform. GDB is used wherever it's available
// (Linux/macOS, and MinGW on Windows); the MSVC engine is the Windows-native
// fallback. The GdbDebugger reports isActive()==false if gdb can't be launched,
// so a null return specifically means "no backend compiled in".
std::unique_ptr<IDebugger> createDebugger() {
#if defined(_WIN32)
    return std::make_unique<MsvcDebugger>();
#else
    return std::make_unique<GdbDebugger>();
#endif
}

} // namespace dbg
