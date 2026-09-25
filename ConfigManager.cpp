#include "ConfigManager.h"
#include "DependencyChecker.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iomanip>

ConfigManager::ConfigManager(const std::string& configPath, const std::string& colorsPath)
    : m_configPath(configPath), m_colorsPath(colorsPath)
{
    // Pin the paths to absolute now, at startup. The file browser chdir()s as the
    // user navigates, so a relative path like "config.json" would otherwise be
    // saved into whatever directory happens to be current - losing the settings.
    std::error_code ec;
    auto absCfg = std::filesystem::absolute(m_configPath, ec);
    if (!ec) m_configPath = absCfg.string();
    auto absCol = std::filesystem::absolute(m_colorsPath, ec);
    if (!ec) m_colorsPath = absCol.string();

    auto abs_dir = std::filesystem::absolute(std::filesystem::path(m_configPath)).parent_path();
    m_sessionPath = (abs_dir / "session.json").string();
}

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
            if (data.contains("use_tab_character")) config.use_tab_character = data["use_tab_character"];
            // text_render_mode supersedes the old boolean "smooth_text"; fall back
            // to it for configs written before the third (Sharp) mode existed.
            if (data.contains("text_render_mode")) config.text_render_mode = data["text_render_mode"];
            else if (data.contains("smooth_text")) config.text_render_mode = data["smooth_text"] ? 1 : 0;
            if (data.contains("rounded_corners")) config.rounded_corners = data["rounded_corners"];
            if (data.contains("editor_font")) config.editor_font = data["editor_font"];
            if (data.contains("show_line_numbers")) config.show_line_numbers = data["show_line_numbers"];
            if (data.contains("syntax_highlight")) config.syntax_highlight = data["syntax_highlight"];
            if (data.contains("show_whitespace"))  config.show_whitespace  = data["show_whitespace"];
            if (data.contains("show_inline_diagnostics")) config.show_inline_diagnostics = data["show_inline_diagnostics"];
            if (data.contains("color_scheme")) config.color_scheme_name = data["color_scheme"];
            if (data.contains("compile_mode")) config.compile_mode = data["compile_mode"];
            if (data.contains("optimization_level")) config.optimization_level = data["optimization_level"];
            if (data.contains("security_flags")) config.security_flags = data["security_flags"].get<std::vector<bool>>();
            if (data.contains("extra_compile_flags")) config.extra_compile_flags = data["extra_compile_flags"];
            if (data.contains("keybindings")) config.keybindings = data["keybindings"].get<std::map<std::string, std::string>>();
            if (data.contains("recent_files")) config.recent_files = data["recent_files"].get<std::vector<std::string>>();
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
    j["use_tab_character"] = config.use_tab_character;
    j["text_render_mode"] = config.text_render_mode;
    j["rounded_corners"] = config.rounded_corners;
    j["editor_font"] = config.editor_font;
    j["show_line_numbers"] = config.show_line_numbers;
    j["syntax_highlight"] = config.syntax_highlight;
    j["show_whitespace"]  = config.show_whitespace;
    j["show_inline_diagnostics"] = config.show_inline_diagnostics;
    j["color_scheme"] = config.color_scheme_name;
    j["compile_mode"] = config.compile_mode;
    j["optimization_level"] = config.optimization_level;
    j["security_flags"] = config.security_flags;
    j["extra_compile_flags"] = config.extra_compile_flags;
    j["keybindings"] = config.keybindings;
    j["recent_files"] = config.recent_files;
    
    std::ofstream o(m_configPath);
    if (o.is_open()) {
        o << std::setw(4) << j << std::endl;
    }
}

void ConfigManager::saveSession(const json& session) {
    std::ofstream o(m_sessionPath);
    if (o.is_open())
        o << std::setw(4) << session << std::endl;
}

json ConfigManager::loadSession() {
    if (!std::filesystem::exists(m_sessionPath)) return json::object();
    try {
        std::ifstream f(m_sessionPath);
        return json::parse(f);
    } catch (...) {
        return json::object();
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

void ConfigManager::loadToolchain(Config& config) {
    std::string path = DependencyChecker::toolchainPath();
    if (!std::filesystem::exists(path)) return;
    try {
        std::ifstream f(path);
        json j = json::parse(f);
        config.toolchain.cc         = j.value("cc",         "");
        config.toolchain.cxx        = j.value("cxx",        "");
        config.toolchain.clang      = j.value("clang",      "");
        config.toolchain.clang_cxx  = j.value("clang_cxx",  "");
        config.toolchain.pkg_config = j.value("pkg_config", "");
        config.toolchain.python3    = j.value("python3",    "");
    } catch (...) {}
}

void ConfigManager::createDefaultConfigFile(const std::string& path) {
    json j;
    j["smart_indentation"] = true;
    j["indentation_width"] = 4;
    j["use_tab_character"] = false;
    j["text_render_mode"] = 1;
    j["rounded_corners"] = false;
    j["editor_font"] = "Default";
    j["show_line_numbers"] = true;
    j["show_inline_diagnostics"] = true;
    j["color_scheme"] = "Obsidian";
    j["compile_mode"] = -1;
    j["optimization_level"] = -1;
    j["security_flags"] = {true, true, true, true, true};
    j["extra_compile_flags"] = "-Wall";
    j["keybindings"] = {
        {"new", "Ctrl+N"}, {"open", "Ctrl+O"}, {"save", "Ctrl+S"}, {"exit", "Alt+X"},
        {"undo", "Alt+BS"}, {"redo", "Alt+Y"}, {"cut", "Ctrl+X"}, {"copy", "Ctrl+C"},
        {"paste", "Ctrl+V"}, {"find", "Ctrl+F"}, {"replace", "Ctrl+R"}, {"compile", "Shift+F9"},
        {"run", "F9"}, {"toggle_output", "F5"}, {"next_buffer", "F6"}, {"prev_buffer", "Shift+F6"},
        {"close_buffer", "Ctrl+W"}, {"toggle_comment", "Ctrl+/"}
    };
    
    std::ofstream o(path);
    if (o.is_open()) {
        o << std::setw(4) << j << std::endl;
    }
}
