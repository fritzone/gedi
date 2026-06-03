#pragma once
#include "DialogBase.h"
#include "Renderer.h"
#include <functional>
#include <string>
#include <vector>

struct Config;
class ConfigManager;

class SettingsDialog : private DialogBase {
public:
    static void show(Renderer& renderer, Config& config, ConfigManager& configManager,
                     std::function<void()> on_background_refresh = {});

private:
    SettingsDialog(Renderer& renderer, Config& config, ConfigManager& configManager);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;
    bool onTab(bool forward) override;
    void applySettings();

    Renderer&       renderer_;
    Config&         config_;
    ConfigManager&  configManager_;
    std::vector<std::string> themes_;

    // Editing tab
    bool temp_smart_indent_;
    int  temp_indent_width_;
    bool temp_show_whitespace_;

    // Display tab
    bool temp_show_line_numbers_;
    int  temp_syntax_highlight_;
    int  temp_syntax_hl_cursor_;

    // Colors tab
    int  temp_theme_selected_;
    int  temp_theme_cursor_;

    static constexpr int GRP_TABS       = 0;
    static constexpr int GRP_EDITING    = 1;
    static constexpr int GRP_DISPLAY_CB = 2;   // Show Line Numbers checkbox
    static constexpr int GRP_DISPLAY_HL = 3;   // Syntax Highlighting radiolist
    static constexpr int GRP_COLORS     = 4;
};
