#ifndef DEPENDENCYCHECKER_H
#define DEPENDENCYCHECKER_H

#include "Config.h"

class DependencyChecker {
public:
    // Returns true if all required tools are available (or the check is skipped).
    // ignore=true: writes "ignore_dependencies":true to toolchain.json and returns true
    //              without probing; all subsequent runs also skip the check.
    // On first run (ignore=false): probes the system, writes toolchain.json, fills `out`.
    // On subsequent runs: reads toolchain.json, fills `out`, returns true.
    // On failure: prints a plain-text error with distro-specific install
    //             commands (including the --ignore-dependencies hint) and returns false.
    static bool check(Toolchain& out, bool ignore = false);

    // Path of the toolchain config file (~/.config/gedi/toolchain.json).
    static std::string toolchainPath();

    // Put the bundled compiler and build systems at the front of PATH for this
    // process and everything it spawns, so plain "cmake" / "ninja" / "meson" /
    // "make" resolve to the shipped copies. Call once, before anything builds.
    // A no-op where nothing is bundled, and on non-Windows.
    static void useBundledTools();
};

#endif // DEPENDENCYCHECKER_H
