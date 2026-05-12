#include "SettingsDialog.h"
#include "ConfigManager.h"  // provides class ConfigManager

// ═══════════════════════════════════════════════════════════════════════════════
// SettingsDialog.cpp
//
// Only what is unique to this dialog lives here.
// Layout (w=60, h=25):
//
//   row  2-5 : ┌─ Indentation ──────────────────────────────┐
//                 [X] Smart Indent
//                 < 4 > Tab Size
//   row  7-9 : ┌─ View ─────────────────────────────────────┐
//                 [X] Show Line Numbers
//   row 11-? : ┌─ Color Scheme ──────────────────────────────┐
//                 ( ) theme1
//                 (•) theme2   ← scrollable
//                 …
//   row h-3  :         [ Save ]   [ Cancel ]
// ═══════════════════════════════════════════════════════════════════════════════

// ── Layout constants (relative to dialog top-left) ────────────────────────────
static constexpr int W             = 60;
static constexpr int H             = 25;
static constexpr int INNER_W       = W - 4;   // 56

static constexpr int INDENT_BOX_Y  = 2;
static constexpr int INDENT_BOX_H  = 4;
static constexpr int VIEW_BOX_Y    = 7;
static constexpr int VIEW_BOX_H    = 3;
static constexpr int COLOR_BOX_Y   = 11;
static constexpr int COLOR_BOX_H   = H - 15;  // 10 rows
static constexpr int LIST_ROWS     = COLOR_BOX_H - 2;
static constexpr int BTN_Y         = H - 3;

// ── Constructor ───────────────────────────────────────────────────────────────

SettingsDialog::SettingsDialog(Config&        config,
                               ConfigManager& configManager,
                               const std::vector<std::string>& themes)
    : DialogBase("Editor Settings", W, H)
    , config_      (config)
    , configManager_(configManager)
    , themes_      (themes)
    , temp_smart_indent_    (config.smart_indentation)
    , temp_indent_width_    (config.indentation_width)
    , temp_show_line_numbers_(config.show_line_numbers)
    , temp_theme_selected_  (0)
    , temp_theme_cursor_    (0)
{
    // Locate the current theme in the list
    for (int i = 0; i < (int)themes.size(); ++i) {
        if (themes[i] == config.color_scheme_name) {
            temp_theme_selected_ = i;
            temp_theme_cursor_   = i;
            break;
        }
    }
}

// ── Static factory ────────────────────────────────────────────────────────────

void SettingsDialog::show(Renderer& renderer,
                          Config& config,
                          ConfigManager& configManager,
                          const std::vector<std::string>& themes)
{
    SettingsDialog dlg(config, configManager, themes);
    dlg.run(renderer);
    // Result is written directly into config by the Save lambda.
}

// ── onInit ────────────────────────────────────────────────────────────────────

void SettingsDialog::onInit()
{
    // ── Group 0: Indentation ─────────────────────────────────────────────────
    {
        FocusGroup g;
        g.title   = " &Indentation ";
        g.hotkey  = 'i';
        g.box_x   = 2;  g.box_y = INDENT_BOX_Y;
        g.box_w   = INNER_W;  g.box_h = INDENT_BOX_H;

        g.checkboxes.push_back({
            .label = "Smart Indent",
            .value = temp_smart_indent_,
            .x = 4, .y = INDENT_BOX_Y + 1,
        });
        g.spinners.push_back({
            .label   = "Tab Size",
            .value   = temp_indent_width_,
            .min_val = 1, .max_val = 16,
            .x = 4, .y = INDENT_BOX_Y + 2,
        });
        addGroup(std::move(g));
    }

    // ── Group 1: View ────────────────────────────────────────────────────────
    {
        FocusGroup g;
        g.title  = " &View ";
        g.hotkey = 'v';
        g.box_x  = 2;  g.box_y = VIEW_BOX_Y;
        g.box_w  = INNER_W;  g.box_h = VIEW_BOX_H;

        g.checkboxes.push_back({
            .label = "Show Line Numbers",
            .value = temp_show_line_numbers_,
            .x = 4, .y = VIEW_BOX_Y + 1,
        });
        addGroup(std::move(g));
    }

    // ── Group 2: Color Scheme ────────────────────────────────────────────────
    {
        FocusGroup g;
        g.title  = " Col&or Scheme ";
        g.hotkey = 'o';
        g.box_x  = 2;  g.box_y = COLOR_BOX_Y;
        g.box_w  = INNER_W;  g.box_h = COLOR_BOX_H;

        g.radiolists.push_back({
            .items        = themes_,
            .selected_idx = temp_theme_selected_,
            .cursor_idx   = temp_theme_cursor_,
            .x = 4, .y = COLOR_BOX_Y + 1,
            .visible_rows = LIST_ROWS,
        });
        addGroup(std::move(g));
    }

    // ── Buttons ───────────────────────────────────────────────────────────────
    addGroupButton({
        .focus_index = 0,
        .btn_x = W / 2 - 15, .btn_y = BTN_Y,
        .label = " &Save ",
        .on_activate = [this]() -> HandleResult {
            config_.smart_indentation = temp_smart_indent_;
            config_.indentation_width = temp_indent_width_;
            config_.show_line_numbers  = temp_show_line_numbers_;
            config_.color_scheme_name  = themes_[temp_theme_selected_];
            configManager_.saveConfig(config_);
            auto themes_map = configManager_.loadThemes();
            auto it = themes_map.find(config_.color_scheme_name);
            // Only reload colors if the theme exists in the map
            // (avoids a crash if loadThemes returns a different key type)
            result().accept();
            return HandleResult::CLOSE;
        }
    });

    addGroupButton({
        .focus_index = 1,
        .btn_x = W / 2 + 5, .btn_y = BTN_Y,
        .label = " &Cancel ",
        .on_activate = [this]() -> HandleResult {
            result().cancel();
            return HandleResult::CLOSE;
        }
    });

    // Start focus on group 0
    setGroupFocus(0);
    setGroupBtnFocus(0);
}

// ── onDraw ────────────────────────────────────────────────────────────────────
// Groups draw themselves (boxes, labels, widgets). Nothing extra is needed here.

void SettingsDialog::onDraw(Renderer& /*renderer*/,
                            int       /*startx*/,
                            int       /*starty*/)
{
}
