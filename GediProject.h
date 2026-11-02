#pragma once

#include "LibraryInfo.h"
#include "CompilerSettings.h"
#include <string>
#include <vector>
#include <string_view>

// A named build target (executable or library) within a project.
struct ProjectTarget {

    enum class TARGET_TYPE : int
    {
        TT_EXECUTABLE = 0,
        TT_STATIC_LIB = 1,
        TT_SHARED_LIB = 2
    };

    static constexpr std::string_view TYPE_KEYS[] = {
        "executable",
        "static_library",
        "shared_library"
    };

    constexpr std::string_view to_str(TARGET_TYPE type) {
        switch (type) {
        case TARGET_TYPE::TT_EXECUTABLE: return TYPE_KEYS[0];
        case TARGET_TYPE::TT_STATIC_LIB: return TYPE_KEYS[1];
        case TARGET_TYPE::TT_SHARED_LIB: return TYPE_KEYS[2];
        }
        return "unknown";
    }

    ProjectTarget() = default;
    ProjectTarget(const std::string& name, const std::vector<std::string>& sources);
    explicit ProjectTarget(const std::string& name);


    bool isExecutable() const;
    bool isSharedLibrary() const;
    bool isStaticLibrary() const;

    std::string abbr() const;

public:

    std::string name = "";
    std::string type { to_str(TARGET_TYPE::TT_EXECUTABLE) }; // "executable", "static_library", "shared_library"
    std::vector<std::string> sources {};      // paths relative to project root
    std::vector<std::string> link_targets {}; // names of other ProjectTargets this links against

};

// Build systems gedi can generate a build file for; the index is the radio
// position in the New Project / Project Properties dialogs.
inline constexpr const char* kBuildSystemKeys[]   = {"cmake", "make", "meson", "watcom"};
inline constexpr const char* kBuildSystemLabels[] = {"CMake", "Make", "Meson", "Watcom"};
inline constexpr int         kBuildSystemCount    = 4;

// Radio index of a build-system key (unknown keys map to CMake).
inline int buildSystemIndex(std::string_view key)
{
    for (int i = 0; i < kBuildSystemCount; ++i)
        if (key == kBuildSystemKeys[i]) return i;
    return 0;
}

// Platforms a gedi-generated Open Watcom project can target. Each one is what
// the generated makefile needs to build for it: the wlink system, the compilers
// and memory model, the -bt= build target and the header directories under
// %WATCOM%. Verified against Open Watcom 2.0 - every entry links a working
// executable of the right format.
struct WatcomPlatform {
    const char* key;          // stored in the project file
    const char* label;        // shown in New Project / Project Properties
    const char* system;       // wlink "system" for executables
    const char* dll_system;   // wlink "system" for DLL targets; "" = no DLLs
    const char* model;        // memory model: -m<model>
    const char* cc;           // C compiler
    const char* cxx;          // C++ compiler
    const char* target;       // -bt=<target>
    const char* extra_flags;  // anything else the target needs ("-zw": Windows 3.x code)
    const char* includes;     // header directories under %WATCOM%, space separated
    const char* exe_ext;      // ".exe", or "" for Linux
    bool        dwarf;        // DWARF debug info (gdb) instead of Watcom's own format
};

inline constexpr WatcomPlatform kWatcomPlatforms[] = {
    { "dos16", "16-bit DOS",           "dos",     "",            "l", "wcc",    "wpp",    "dos",     "",    "h",        ".exe", false },
    { "dos32", "32-bit DOS (DOS/4GW)", "dos4g",   "",            "f", "wcc386", "wpp386", "dos",     "",    "h",        ".exe", false },
    { "win16", "16-bit Windows 3.x",   "windows", "windows_dll", "l", "wcc",    "wpp",    "windows", "-zw", "h h/win",  ".exe", false },
    { "win32", "32-bit Windows",       "nt",      "nt_dll",      "f", "wcc386", "wpp386", "nt",      "",    "h h/nt",   ".exe", false },
    { "os2",   "32-bit OS/2",          "os2v2",   "os2v2_dll",   "f", "wcc386", "wpp386", "os2",     "",    "h h/os2",  ".exe", false },
    { "linux", "32-bit Linux",         "linux",   "",            "f", "wcc386", "wpp386", "linux",   "",    "lh",       "",     true  },
};
inline constexpr int kWatcomPlatformCount = 6;

// Index of a platform key; unknown keys (and projects from before platforms
// existed) map to 16-bit DOS, which is what those projects were built for.
inline int watcomPlatformIndex(std::string_view key)
{
    for (int i = 0; i < kWatcomPlatformCount; ++i)
        if (key == kWatcomPlatforms[i].key) return i;
    return 0;
}

// The platform whose programs gedi can run and debug where it is running.
inline constexpr const char* kWatcomHostPlatform =
#ifdef _WIN32
    "win32";
#else
    "linux";
#endif

// Internal project model.  Serialised to/from a .gproj text file whose
// location determines the project root (root is never stored in the file).
struct GediProject {
    int         version      = 1;
    std::string name;
    std::string root;           // absolute path to the project root directory
    std::string build_system = "cmake";   // "cmake", "make", "meson", "watcom" (Open Watcom wmake)
    std::string cpp_standard = "c++17";  // kept for legacy compat; authoritative copy is in compiler_settings
    std::string watcom_platform = "dos16"; // build_system "watcom": key into kWatcomPlatforms

    CompilerSettings compiler_settings;  // build flags applied when gedi drives the build

    std::vector<std::string> sources;   // paths relative to root (legacy; used for migration)
    std::vector<ProjectTarget> targets; // named build targets
    std::vector<LibraryInfo> libraries;

    // Imported projects (see MakefileImporter): the build file already exists
    // and is maintained by the user.  gedi builds with it but never rewrites it.
    std::string build_file;                    // relative to root; empty = generated by gedi
    std::string make_tool;                     // "wmake" for Open Watcom makefiles; empty = GNU make
    std::vector<std::string> aux_build_files;  // included / sub-directory makefiles, relative to root
    std::vector<std::string> include_dirs;     // -I directories, relative to root (code intelligence)
    std::vector<std::string> defines;          // -D definitions (code intelligence)

    bool hasExternalBuildFile() const { return !build_file.empty(); }

    // Built with Open Watcom's wmake: an imported Watcom makefile, or a
    // project gedi generates a Watcom makefile for.
    bool usesWmake() const { return make_tool == "wmake" || build_system == "watcom"; }

    // A gedi-generated Open Watcom project's platform.
    const WatcomPlatform& watcomPlatform() const
    { return kWatcomPlatforms[watcomPlatformIndex(watcom_platform)]; }

    // Its programs can run here: a generated Watcom project targeting this
    // host. Imported Watcom makefiles target whatever they say, which gedi
    // does not know, so they never qualify.
    bool watcomRunsOnHost() const
    {
        return build_system == "watcom" && !hasExternalBuildFile() &&
               std::string_view(watcomPlatform().key) == kWatcomHostPlatform;
    }

    // -I/-D flags (absolute include paths) for libclang, from include_dirs/defines.
    std::string codeModelFlags() const;

    // Returns the canonical file path: <root>/<name>.gproj
    std::string projectFilePath() const;

    // Save to the canonical file path.
    bool save() const;

    // Load from an explicit file path.  On success, populates `out` and
    // sets out.root to the directory that contains the file.
    static bool load(const std::string& path, GediProject& out);

    // gets the build file name from the project type
    std::string buildFile() const;

    std::string buildFilePath() const;

    bool removeFileFromBuildSystem(const std::string& file_to_remove, const std::string &rel);

    bool removeFileFromCMakeLists(const std::string& cmake_path, const std::string& rel_path);

    bool removeFileFromMakefile(const std::string& project_root, const std::string& rel_path);

    bool removeFileFromMesonBuild(const std::string& project_root, const std::string& rel_path);

    bool addFileToMakefile(const std::string& project_root, const std::string& rel_path);

    bool addFileToMesonBuild(const std::string& project_root, const std::string& rel_path);

    bool addFileToCMakeLists(const std::string& cmake_path, const std::string& new_file);
};
