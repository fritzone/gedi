#pragma once
#include "DialogBase.h"
#include "Renderer.h"
#include <string>
#include <vector>

// Forward declarations — these come from the application
struct Config;
class  ConfigManager;

// ═══════════════════════════════════════════════════════════════════════════════
// SettingsDialog
//
// Modal "Editor Settings" dialog.
// On accept, writes back into `config` and calls configManager.saveConfig().
// On cancel, config is left unchanged.
// ═══════════════════════════════════════════════════════════════════════════════

class SettingsDialog : private DialogBase {
public:
    static void show(Renderer&      renderer,
                     Config&        config,
                     ConfigManager& configManager,
                     const std::vector<std::string>& themes);

private:
    SettingsDialog(Config&        config,
                   ConfigManager& configManager,
                   const std::vector<std::string>& themes);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;

    // ── Application state references ─────────────────────────────────────────
    Config&        config_;
    ConfigManager& configManager_;
    const std::vector<std::string>& themes_;

    // ── Temporary working copies (edited until Save is pressed) ──────────────
    bool temp_smart_indent_;
    int  temp_indent_width_;
    bool temp_show_line_numbers_;
    int  temp_theme_selected_;   // committed selection (• marker)
    int  temp_theme_cursor_;     // highlighted row in the list
};
