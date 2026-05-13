#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <string>
#include <vector>
#include "Config.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;


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
