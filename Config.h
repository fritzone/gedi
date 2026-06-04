#ifndef CONFIG_H
#define CONFIG_H

#include <map>
#include <string>
#include <vector>

struct Toolchain {
    std::string cc;
    std::string cxx;
    std::string clang;
    std::string clang_cxx;
    std::string pkg_config;
    std::string python3;
};

struct Config {
    bool smart_indentation = true;
    int indentation_width = 4;
    bool show_line_numbers = true;
    int syntax_highlight = 2;    // 0=none  1=basic  2=advanced (clang)
    bool show_whitespace = false;
    std::string color_scheme_name = "Obsidian";
    int compile_mode = -1;
    int optimization_level = -1;
    std::vector<bool> security_flags = {true, true, true, true, true};
    std::string extra_compile_flags = "-Wall";
    std::map<std::string, std::string> keybindings;
    std::vector<std::string> recent_files;  // MRU list, newest first, max 10
    Toolchain toolchain;
};


#endif // CONFIG_H
