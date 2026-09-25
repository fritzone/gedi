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
    bool temp_use_tab_char_;
    bool temp_show_whitespace_;
    int  temp_render_mode_;       // graphical build only: 0=Pixelated 1=Smooth 2=Sharp
    int  temp_render_cursor_;     // graphical build only: radiolist cursor

    // Display tab
    bool temp_show_line_numbers_;
    bool temp_inline_diag_;       // Error-Lens style inline diagnostic text
    bool temp_rounded_corners_;   // graphical build only
    int  temp_syntax_highlight_;
    int  temp_syntax_hl_cursor_;

    // Display tab - editor font list (graphical build only)
    std::vector<std::string> font_names_;   // human-readable names ("Default" first)
    std::vector<std::string> font_paths_;   // matching file paths ("" for Default)
    std::vector<int>         font_indices_; // engine registry ids (to preview each font)
    int  temp_font_selected_ = 0;
    int  temp_font_cursor_   = 0;

    // Colors tab
    int  temp_theme_selected_;
    int  temp_theme_cursor_;

    static constexpr int GRP_TABS         = 0;
    static constexpr int GRP_EDITING      = 1;
    static constexpr int GRP_DISPLAY_CB   = 2;   // Show Line Numbers checkbox
    static constexpr int GRP_DISPLAY_HL   = 3;   // Syntax Highlighting radiolist
    static constexpr int GRP_COLORS       = 4;
    static constexpr int GRP_DISPLAY_FONT = 5;   // Editor font radiolist (graphical only)
    static constexpr int GRP_EDITING_RENDER = 6; // Text Rendering radiolist (graphical only)
};
