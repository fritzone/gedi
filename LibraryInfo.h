#pragma once
#include <string>
#include <vector>

struct LibraryInfo {
    std::string short_name;
    std::string version;
    std::string description;
    std::string cmake_find_package_hint;
    std::vector<std::string> include_directories;
    std::vector<std::string> link_libraries;
    std::vector<std::string> link_directories;
    std::vector<std::string> compiler_flags;
};
