#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <string>
#include <vector>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

struct Config {
    bool smart_indentation = true;
    int indentation_width = 4;
    bool show_line_numbers = true;
    std::string color_scheme_name = "Obsidian";
    int compile_mode = -1;
    int optimization_level = -1;
    std::vector<bool> security_flags = {true, true, true, true, true};
    std::string extra_compile_flags = "-Wall";
};

class ConfigManager {
public:
    ConfigManager(const std::string& configPath, const std::string& colorsPath);
    
    void loadConfig(Config& config);
    void saveConfig(const Config& config);
    json loadThemes();

private:
    std::string m_configPath;
    std::string m_colorsPath;
    void createDefaultConfigFile(const std::string& path);
};

#endif // CONFIGMANAGER_H
