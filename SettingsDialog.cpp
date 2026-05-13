#include "SettingsDialog.h"
#include "Config.h"
#include "ConfigManager.h"

static constexpr int W            = 60;
static constexpr int H            = 25;
static constexpr int INNER_W      = W - 4;
static constexpr int INDENT_BOX_Y = 2;
static constexpr int INDENT_BOX_H = 4;
static constexpr int VIEW_BOX_Y   = 7;
static constexpr int VIEW_BOX_H   = 3;
static constexpr int COLOR_BOX_Y  = 11;
static constexpr int COLOR_BOX_H  = H - 15;
static constexpr int LIST_ROWS    = COLOR_BOX_H - 2;
static constexpr int BTN_Y        = H - 3;

SettingsDialog::SettingsDialog(Renderer& renderer, Config& config,
                               ConfigManager& configManager,
                               const std::vector<std::string>& themes)
    : DialogBase("Editor Settings", W, H)
    , renderer_(renderer)
    , config_(config), configManager_(configManager), themes_(themes)
    , temp_smart_indent_    (config.smart_indentation)
    , temp_indent_width_    (config.indentation_width)
    , m_temp_show_line_numbers(config.show_line_numbers)
    , m_temp_theme_selected  (0)
    , m_temp_theme_cursor    (0)
{
    for (int i = 0; i < (int)themes.size(); ++i) {
        if (themes[i] == config.color_scheme_name) {
            m_temp_theme_selected = m_temp_theme_cursor = i;
            break;
        }
    }
}

void SettingsDialog::show(Renderer& renderer, Config& config,
                          ConfigManager& configManager,
                          const std::vector<std::string>& themes)
{
    SettingsDialog dlg(renderer, config, configManager, themes);
    dlg.run(renderer);
}

void SettingsDialog::onInit()
{
    // ── Group 0: Indentation ──────────────────────────────────────────────────
    {
        FocusGroup g;
        g.title  = " &Indentation ";
        g.hotkey = 'i';
        g.box_x  = 2;  g.box_y = INDENT_BOX_Y;
        g.box_w  = INNER_W;  g.box_h = INDENT_BOX_H;
        g.checkboxes.push_back({ "Smart Indent", temp_smart_indent_,
                                4, INDENT_BOX_Y + 1 });
        g.spinners.push_back  ({ "Tab Size", temp_indent_width_, 1, 16,
                              4, INDENT_BOX_Y + 2 });
        addGroup(std::move(g));
    }

    // ── Group 1: View ─────────────────────────────────────────────────────────
    {
        FocusGroup g;
        g.title  = " &View ";
        g.hotkey = 'v';
        g.box_x  = 2;  g.box_y = VIEW_BOX_Y;
        g.box_w  = INNER_W;  g.box_h = VIEW_BOX_H;
        g.checkboxes.push_back({ "Show Line Numbers", m_temp_show_line_numbers,
                                4, VIEW_BOX_Y + 1 });
        addGroup(std::move(g));
    }

    // ── Group 2: Color Scheme ─────────────────────────────────────────────────
    {
        FocusGroup g;
        g.title  = " Col&or Scheme ";
        g.hotkey = 'o';
        g.box_x  = 2;  g.box_y = COLOR_BOX_Y;
        g.box_w  = INNER_W;  g.box_h = COLOR_BOX_H;
        g.radiolists.push_back({ themes_, m_temp_theme_selected, m_temp_theme_cursor,
                                4, COLOR_BOX_Y + 1, LIST_ROWS });
        addGroup(std::move(g));
    }

    // ── Button row ────────────────────────────────────────────────────────────
    // Layout (w=60): [ Save ] [ Apply ] [ Cancel ]
    // Widths: 7 + 8 + 9 = 24, gaps = 4 → total 28 → startx = (60-28)/2 = 16
    static constexpr int BTN_SAVE_X   = 16;
    static constexpr int BTN_APPLY_X  = BTN_SAVE_X  + 7 + 2;   // 25
    static constexpr int BTN_CANCEL_X = BTN_APPLY_X + 8 + 2;   // 35

    addButtons(ButtonRow{
        .buttons = {
            Button{
                .label = " &Save ",
                .x = BTN_SAVE_X, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    applySettings();
                    configManager_.saveConfig(config_);
                    result().accept();
                    return HandleResult::CLOSE;
                }
            },
            Button{
                .label = " &Apply ",
                .x = BTN_APPLY_X, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    applySettings();
                    return HandleResult::CONTINUE;   // keep dialog open
                }
            },
            Button{
                .label = " &Cancel ",
                .x = BTN_CANCEL_X, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    result().cancel();
                    return HandleResult::CLOSE;
                }
            },
        }
    });

    setGroupFocus(0);
    setGroupBtnFocus(0);
}


// ── applySettings: write temps into config and reload colours ─────────────────

void SettingsDialog::applySettings()
{
    config_.smart_indentation = temp_smart_indent_;
    config_.indentation_width = temp_indent_width_;
    config_.show_line_numbers  = m_temp_show_line_numbers;
    config_.color_scheme_name  = themes_[m_temp_theme_selected];
    renderer_.loadColors(configManager_.loadThemes()[config_.color_scheme_name]);
}
void SettingsDialog::onDraw(Renderer&, int, int) {}
