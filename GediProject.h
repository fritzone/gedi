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

// Internal project model.  Serialised to/from a .gproj text file whose
// location determines the project root (root is never stored in the file).
struct GediProject {
    int         version      = 1;
    std::string name;
    std::string root;           // absolute path to the project root directory
    std::string build_system = "cmake";   // "cmake", "make", "meson"
    std::string cpp_standard = "c++17";  // kept for legacy compat; authoritative copy is in compiler_settings

    CompilerSettings compiler_settings;  // build flags applied when gedi drives the build

    std::vector<std::string> sources;   // paths relative to root (legacy; used for migration)
    std::vector<ProjectTarget> targets; // named build targets
    std::vector<LibraryInfo> libraries;

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
