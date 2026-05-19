#pragma once

#include "LibraryInfo.h"
#include "CompilerSettings.h"
#include <string>
#include <vector>

// A named build target (executable or library) within a project.
struct ProjectTarget {
    std::string name;
    std::string type = "executable"; // "executable", "static_library", "shared_library"
    std::vector<std::string> sources;      // paths relative to project root
    std::vector<std::string> link_targets; // names of other ProjectTargets this links against
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
};
