#include "ConfigManager.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iomanip>

ConfigManager::ConfigManager(const std::string& configPath, const std::string& colorsPath)
    : m_configPath(configPath), m_colorsPath(colorsPath) {}

void ConfigManager::loadConfig(Config& config) {
    if (!std::filesystem::exists(m_configPath)) {
        createDefaultConfigFile(m_configPath);
    }
    
    try {
        std::ifstream f(m_configPath);
        if (f.is_open()) {
            json data = json::parse(f);
            if (data.contains("smart_indentation")) config.smart_indentation = data["smart_indentation"];
            if (data.contains("indentation_width")) config.indentation_width = data["indentation_width"];
            if (data.contains("show_line_numbers")) config.show_line_numbers = data["show_line_numbers"];
            if (data.contains("color_scheme")) config.color_scheme_name = data["color_scheme"];
            if (data.contains("compile_mode")) config.compile_mode = data["compile_mode"];
            if (data.contains("optimization_level")) config.optimization_level = data["optimization_level"];
            if (data.contains("security_flags")) config.security_flags = data["security_flags"].get<std::vector<bool>>();
            if (data.contains("extra_compile_flags")) config.extra_compile_flags = data["extra_compile_flags"];
        }
    } catch (const json::parse_error& e) {
        // We can't easily call msgwin here without a pointer to TextEditor or a callback.
        // For now, we'll just use defaults if it fails.
        std::cerr << "Error parsing config: " << e.what() << std::endl;
    }
}

void ConfigManager::saveConfig(const Config& config) {
    json j;
    j["smart_indentation"] = config.smart_indentation;
    j["indentation_width"] = config.indentation_width;
    j["show_line_numbers"] = config.show_line_numbers;
    j["color_scheme"] = config.color_scheme_name;
    j["compile_mode"] = config.compile_mode;
    j["optimization_level"] = config.optimization_level;
    j["security_flags"] = config.security_flags;
    j["extra_compile_flags"] = config.extra_compile_flags;
    
    std::ofstream o(m_configPath);
    if (o.is_open()) {
        o << std::setw(4) << j << std::endl;
    }
}

json ConfigManager::loadThemes() {
    if (std::filesystem::exists(m_colorsPath)) {
        try {
            std::ifstream f(m_colorsPath);
            return json::parse(f);
        } catch (const json::parse_error& e) {
            std::cerr << "Error parsing themes: " << e.what() << std::endl;
        }
    }
    return json::object();
}

void ConfigManager::createDefaultConfigFile(const std::string& path) {
    json j;
    j["smart_indentation"] = true;
    j["indentation_width"] = 4;
    j["show_line_numbers"] = true;
    j["color_scheme"] = "Obsidian";
    j["compile_mode"] = -1;
    j["optimization_level"] = -1;
    j["security_flags"] = {true, true, true, true, true};
    j["extra_compile_flags"] = "-Wall";
    
    std::ofstream o(path);
    if (o.is_open()) {
        o << std::setw(4) << j << std::endl;
    }
}
