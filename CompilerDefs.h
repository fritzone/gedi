#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct CompilerSettings;

// ═══════════════════════════════════════════════════════════════════════════════
// Compiler definitions
//
// Everything gedi needs to know about a C++ compiler, read from JSON files
// rather than written into the code, so a compiler gedi has never heard of -
// a cross compiler, a vendor fork, a hobby or long-unmaintained one - can be
// introduced by describing it instead of by changing gedi:
//
//   - how to recognise it (executable names, version text, target platform),
//   - how to invoke it (output switch, standard switch, flags it always gets),
//   - what its errors look like,
//   - what it can do here (run its programs, debug them, CMake / make / Meson),
//   - and its option catalogue: the tabs, checkboxes and radio groups that
//     Build > Compile Options shows for it, each with its command-line switch.
//
// Shipped definitions live in compilers/ next to the executable (or in the
// installed share directory); the user's own in ~/.config/gedi/compilers/
// (%USERPROFILE%\.config\gedi\compilers on Windows), where a file with the
// same id replaces the shipped one. A definition can inherit another
// ("inherits": "gcc") and change only what differs. compilers/README.md
// documents the format.
// ═══════════════════════════════════════════════════════════════════════════════
namespace compilers {

// One choice of a radio option.
struct Choice {
    std::string label;
    std::string flag;         // added to the compiler command line
    std::string cmake_flag;   // added to CMAKE_CXX_FLAGS instead (defaults to flag)
};

// A checkbox (choices empty) or a radio group (choices set).
struct Option {
    std::string id;
    std::string label;
    std::string flag, cmake_flag;      // checkbox: added when ticked
    std::vector<Choice> choices;       // radio group
    int  def = 0;                      // default: 0/1, or the index of the default choice
    std::string role;                  // "debug" / "optimization": picks the CMake build type

    bool radio() const { return !choices.empty(); }
};

struct Group { std::string name; std::vector<Option> options; };
struct Tab   { std::string name; std::vector<Group> groups; };

struct Definition {
    std::string id;
    std::string name;
    std::string description;
    std::filesystem::path source;      // the file it was read from
    bool user     = false;             // from the user's directory
    bool abstract = false;             // a base for others, not offered as a compiler
    int  order    = 100;               // position in the compiler list
    std::vector<std::string> platforms;     // "linux", "windows", "macos"; empty = all

    // ── Recognising it ────────────────────────────────────────────────────
    std::vector<std::string> executables;   // file names, '*' as wildcard, no ".exe"
    std::string version_args  = "--version";
    std::string version_regex;              // when set, the version output must match it
    std::string target;                     // "host" / "cross": compared with -dumpmachine
    int priority = 0;                       // tried before others of the same name match

    // How its environment is set up: "gcc", "clang", "msvc", "watcom" (the
    // installer's detection logic) or "plain" (its directory goes on PATH).
    std::string setup = "plain";

    // ── Invoking it ───────────────────────────────────────────────────────
    std::vector<std::string> args;          // right after the executable ("zig c++")
    std::vector<std::string> always;        // switches every compile gets
    std::string output   = "-o {out}";      // how the executable is named
    std::string compile_only  = "-c";       // compile without linking
    std::string object_output = "-o {out}"; // how the object file is named, with compile_only
    std::string include  = "-I{dir}";
    std::string define   = "-D{def}";
    std::string exe_ext  = "host";          // "host" = ".exe" on Windows only; else literal
    bool host_libraries  = true;            // keep host -l/-L and /usr/... includes
    bool scratch_dir     = false;           // compile from an empty directory
    std::vector<std::string> keep_outputs;  // extensions moved out of that directory

    std::string standard_flag;              // "-std={std}"; empty = no standard switch
    std::vector<std::string> standards;     // offered in the dialogs
    std::map<std::string, std::string> standard_flags;   // per-standard overrides

    std::optional<Option> top;              // shown in the top row instead of the standard
    std::vector<Tab> tabs;
    std::string settings_id;                // whose stored option values these are

    // ── Using it ──────────────────────────────────────────────────────────
    bool runs_on_host = true;               // its programs run on this computer
    bool debuggable   = true;               // gdb / the platform debugger can debug them
    std::string parse_headers = "self";     // libclang's system headers: "self" or "host"

    std::string diagnostics = "gnu";        // "gnu", "msvc", "watcom", or "regex"
    std::string diag_regex;                 // with "regex"; groups below
    int diag_file = 1, diag_line = 2, diag_col = 0, diag_severity = 3, diag_message = 4;

    struct CMake {
        bool supported = true;
        std::string generator;              // -G; empty = CMake's default
        std::string compiler;               // a sibling executable CMake must use instead
        std::string build_dir = "build";
        bool optional_flags = true;         // pass the optional flags in CMAKE_CXX_FLAGS
    } cmake;
    bool make  = true;
    bool meson = true;

    // ── Helpers ───────────────────────────────────────────────────────────
    std::string argsString() const;         // " c++" - args with a leading space, or ""
    std::string standardFlag(const std::string& standard) const;
    std::string exeExtension() const;
    std::string outputPrefix() const;       // "-o " / "/Fe:" - what precedes the output path
    const Option* findOption(const std::string& id) const;
    std::vector<const Option*> allOptions() const;   // top first, then tabs in order
};

// The value an option has in `s` - stored, or the definition's default.
int optionValue(const CompilerSettings& s, const Definition& d, const Option& o);
// Fill every option of `d` the settings do not mention yet with its default,
// so a dialog can bind to them.
void fillDefaults(CompilerSettings& s, const Definition& d);
// The flags the settings amount to, for the compiler driver or for CMake.
std::string flagsFor(const CompilerSettings& s, const Definition& d, bool for_cmake);
// "Debug", "RelWithDebInfo" or "Release", from the options with a role.
std::string cmakeBuildType(const CompilerSettings& s, const Definition& d);
// The optional-flags string for this definition's settings.
const std::string& optionalFlags(const CompilerSettings& s, const Definition& d);

// Keys of options a compiler rejected when tested: "<id>" for a checkbox,
// "<id>:<choice index>" for a radio choice.
std::set<std::string> testedUnsupported(const Definition& d, const std::string& exe);
void saveTestedUnsupported(const Definition& d, const std::string& exe,
                           const std::set<std::string>& unsupported);

struct RawDef;   // a definition file as read, before inheritance (CompilerDefs.cpp)

class Registry {
public:
    static Registry& instance();

    // Read every definition: the shipped ones (looked for around exe_dir and in
    // the installed share directories), then the user's. Safe to call again.
    void load(const std::filesystem::path& exe_dir);
    void reload();

    // Selectable compilers, in display order (abstract bases left out).
    std::vector<const Definition*> selectable() const;
    const Definition* find(const std::string& id) const;

    // The definition an executable is: by name, version text and target.
    // nullptr when none fits.
    const Definition* identify(const std::string& exe) const;

    // The definition for the recorded toolchain: by its recorded id, else by
    // identifying its compiler, else by its "kind", else GCC.
    const Definition& forToolchain(const std::string& compiler_id, const std::string& kind,
                                   const std::string& cxx) const;

    // The definition's compiler as found on PATH ("" if not there).
    std::string findOnPath(const Definition& d) const;

    // Write a new user definition inheriting `base`; returns its id ("" on error).
    std::string addUser(const std::string& name, const std::string& base,
                        const std::string& executable, bool cross, std::string& error);

    std::filesystem::path userDir() const;
    const std::vector<std::string>& problems() const { return problems_; }

private:
    void loadDir(const std::filesystem::path& dir, bool user,
                 std::map<std::string, RawDef>& raw);

    std::filesystem::path exe_dir_;
    bool loaded_ = false;
    std::vector<Definition> defs_;
    std::vector<std::string> problems_;
};

// The host's notion of "runs here", for a GNU-style target triple.
bool targetIsHost(const std::string& triple);

} // namespace compilers
