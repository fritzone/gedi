#pragma once
#include "DialogBase.h"
#include "Renderer.h"
#include <string>
#include <vector>

struct Config;
class ConfigManager;

class SettingsDialog : private DialogBase {
public:
    static void show(Renderer& renderer, Config& config, ConfigManager& configManager, const std::vector<std::string>& themes);

private:
    SettingsDialog(Renderer& renderer, Config& config, ConfigManager& configManager, const std::vector<std::string>& themes);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;
    void applySettings();

    Renderer& renderer_;
    Config& config_;
    ConfigManager& configManager_;
    const std::vector<std::string>& themes_;

    bool temp_smart_indent_;
    int temp_indent_width_;
    bool m_temp_show_line_numbers;
    int m_temp_theme_selected;
    int m_temp_theme_cursor;
};
