#ifndef CONFIG_H
#define CONFIG_H

#include <map>
#include <string>

struct Config {
    bool smart_indentation = true;
    int indentation_width = 4;
    bool show_line_numbers = true;
    std::string color_scheme_name = "Obsidian";
    int compile_mode = -1;
    int optimization_level = -1;
    std::vector<bool> security_flags = {true, true, true, true, true};
    std::string extra_compile_flags = "-Wall";
    std::map<std::string, std::string> keybindings;
};


#endif // CONFIG_H
