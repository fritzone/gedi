#pragma once

#include "LibraryInfo.h"
#include <string>
#include <vector>

// Internal project model.  Serialised to/from a .gproj text file whose
// location determines the project root (root is never stored in the file).
struct GediProject {
    int         version      = 1;
    std::string name;
    std::string root;           // absolute path to the project root directory
    std::string build_system = "cmake";   // "cmake", "make", "meson"
    std::string cpp_standard = "c++17";

    std::vector<std::string> sources;   // paths relative to root
    std::vector<LibraryInfo> libraries;

    // Returns the canonical file path: <root>/<name>.gproj
    std::string projectFilePath() const;

    // Save to the canonical file path.
    bool save() const;

    // Load from an explicit file path.  On success, populates `out` and
    // sets out.root to the directory that contains the file.
    static bool load(const std::string& path, GediProject& out);
};
