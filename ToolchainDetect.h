#pragma once
#include <map>
#include <string>
#include <vector>

// Finding and recording a C++ toolchain: Clang, GCC or Open Watcom anywhere,
// and Visual Studio on Windows.
//
// Shared by setup and the editor. Setup uses it when the user declines the
// bundled Clang, so a machine that already has one of these can skip the
// toolchain component and still build; the editor uses it from Build Options,
// so the compiler can be identified and changed without reinstalling.
//
// gedi learns which compiler to run from one file - ~/.config/gedi/toolchain.json
// (%USERPROFILE% on Windows) - which is what write() below produces.
//
// A compiler path on its own is not enough on Windows. cl.exe with no INCLUDE
// and LIB finds neither <iostream> nor kernel32.lib, and it shells out to a
// link.exe it expects on PATH; the Open Watcom tools refuse to run at all
// without WATCOM and their own INCLUDE. So a Setup carries the directories and
// variables the compiler needs next to its path, and the editor applies them
// before it spawns anything.
namespace toolchain {

enum class Kind {
    None,     // nothing configured - the user will sort it out in the editor
    Msvc,
    Clang,
    Gcc,
    Watcom,
};

struct Setup {
    Kind        kind = Kind::None;
    std::string cc;                                 // C compiler, absolute path
    std::string cxx;                                // C++ compiler, absolute path
    std::vector<std::string>           bin_dirs;    // prepended to PATH
    std::map<std::string, std::string> env;         // INCLUDE, LIB, WATCOM, ...
    std::vector<std::string>           notes;       // what was found, for the UI
    std::string definition;   // the editor's compiler definition id (CompilerDefs.h), if known

    bool ok() const { return !cxx.empty(); }
};

// The identifier stored in toolchain.json, and what the UI shows.
const char* id(Kind k);
const char* label(Kind k);

// id() read back. Anything unrecognised - including the empty string a probed
// toolchain leaves behind - comes back as None.
Kind kindFromId(const std::string& s);

// The first line of a compiler's own version banner, or "" when it will not
// say. This runs the program, so call it when the selection changes rather than
// on every repaint.
std::string versionOf(const std::string& exe);

// The C++ compiler's file name, and the one-line prompt asking for it.
const char* exeName(Kind k);
const char* prompt(Kind k);

// Semicolon-separated globs for a file dialog's type filter: every name this
// kind's compiler is plausibly installed under, not just the canonical one.
const char* browsePattern(Kind k);

// Where a Browse dialog should start looking for this kind: the usual install
// location if it is there, otherwise the drive it would be under. Empty when
// there is no sensible guess. `current` is whatever the user has typed so far,
// which beats any guess when it points somewhere real.
std::string suggestedDir(Kind k, const std::string& current = "");

// True when this kind is configured from a path the user supplies, as opposed
// to being located on its own.
bool needsPath(Kind k);

// Every kind this platform can use, in display order (MSVC only on Windows).
const std::vector<Kind>& kinds();

// Visual Studio, found without help: vswhere when it is present, the standard
// install directories otherwise. Picks up the Windows SDK too, without which
// the MSVC headers are useless. On failure the result is !ok() and `error` says
// why.
Setup detectMsvc(std::string& error);

// The kind's compiler as found on PATH, or "" when it is not there (and always
// for MSVC, which is not configured from a path).
std::string findOnPath(Kind k);

// The other kinds, built from a path the user gave: either the compiler
// executable itself or a directory containing it.
Setup fromPath(Kind k, const std::string& path, std::string& error);

// Any other compiler: the executable itself, its directory on PATH and nothing
// else. For compilers described only by a definition file.
Setup fromPathPlain(const std::string& path, std::string& error);

// Everything a program writes to stdout/stderr when run with `args`, without a
// console window and with stdin closed.
std::string runOutput(const std::string& exe, const std::string& args);

// The python interpreter gedi insists on at startup. Empty when it is not on PATH.
std::string findPython();

// Where gedi reads the toolchain from, and writing it there.
std::string configPath();
bool write(const Setup& s, const std::string& python3, std::string& error);

} // namespace toolchain
