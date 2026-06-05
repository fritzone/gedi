#include "SettingsDialog.h"
#include "Config.h"
#include "ConfigManager.h"
#include <algorithm>

static constexpr int W          = 60;
static constexpr int H          = 22;   // fits an 80x25 screen (was 28)
static constexpr int INNER_W    = W - 4;
static constexpr int TAB_Y      = 2;
static constexpr int CONTENT_Y  = 4;
static constexpr int CONTENT_H  = H - 7;   // = 15 (rows 4..18)
static constexpr int BTN_Y      = H - 3;   // = 19

// Widget y-positions inside the content box (dialog-relative)
// Tab 0 – Editing  (innerCount order: cb[0], cb[1], sp[0])
static constexpr int ED_SMART_Y = CONTENT_Y + 2;   // inner_focus 0
static constexpr int ED_WSPC_Y  = CONTENT_Y + 3;   // inner_focus 1
static constexpr int ED_TABSZ_Y = CONTENT_Y + 5;   // inner_focus 2  (blank gap above)

// Tab 1 – Display
static constexpr int DI_LNUM_Y  = CONTENT_Y + 2;   // Show Line Numbers checkbox
static constexpr int DI_SYNH_LY = CONTENT_Y + 4;   // "Syntax Highlighting:" label row
static constexpr int DI_SYNH_Y  = CONTENT_Y + 5;   // radiolist start

// Tab 2 – Colors
static constexpr int CL_LIST_Y    = CONTENT_Y + 2;
static constexpr int CL_LIST_ROWS = CONTENT_H - 4; // = 11

// ── Constructor ───────────────────────────────────────────────────────────────

SettingsDialog::SettingsDialog(Renderer& renderer, Config& config,
                               ConfigManager& configManager)
    : DialogBase("Editor Settings", W, H)
    , renderer_(renderer)
    , config_(config), configManager_(configManager)
    , temp_smart_indent_    (config.smart_indentation)
    , temp_indent_width_    (config.indentation_width)
    , temp_show_whitespace_ (config.show_whitespace)
    , temp_show_line_numbers_(config.show_line_numbers)
    , temp_syntax_highlight_(std::max(0, std::min(2, config.syntax_highlight)))
    , temp_syntax_hl_cursor_(std::max(0, std::min(2, config.syntax_highlight)))
    , temp_theme_selected_  (0)
    , temp_theme_cursor_    (0)
{
    auto themes_json = configManager_.loadThemes();
    for (auto const& [key, val] : themes_json.items())
        themes_.push_back(key);
    std::sort(themes_.begin(), themes_.end());

    for (int i = 0; i < (int)themes_.size(); ++i) {
        if (themes_[i] == config.color_scheme_name) {
            temp_theme_selected_ = temp_theme_cursor_ = i;
            break;
        }
    }
}

// ── show() ────────────────────────────────────────────────────────────────────

void SettingsDialog::show(Renderer& renderer, Config& config,
                          ConfigManager& configManager,
                          std::function<void()> on_background_refresh)
{
    SettingsDialog dlg(renderer, config, configManager);
    if (on_background_refresh)
        dlg.setBackgroundRefresh(std::move(on_background_refresh));
    dlg.run(renderer);
}

// ── onInit() ──────────────────────────────────────────────────────────────────

void SettingsDialog::onInit()
{
    // ── Group 0: Tab bar ──────────────────────────────────────────────────────
    {
        FocusGroup g;
        g.title = ""; g.hotkey = '\0';
        g.box_w = 0; g.box_h = 0; g.box_x = 0; g.box_y = 0;
        g.tabcontrols.push_back(TabControl({"Editing", "Display", "Colors"}, 2, TAB_Y, INNER_W));
        addGroup(std::move(g));
    }

    // ── Group 1: Editing tab ──────────────────────────────────────────────────
    // innerCount order: checkboxes[0], checkboxes[1], spinners[0]
    {
        FocusGroup g;
        g.title = " Editing "; g.hotkey = '\0';
        g.box_x = 2; g.box_y = CONTENT_Y; g.box_w = INNER_W; g.box_h = CONTENT_H;
        g.draw_widgets_manually = true;
        g.checkboxes.push_back({ "Smart Indent",    temp_smart_indent_,    4, ED_SMART_Y });
        g.checkboxes.push_back({ "Show Whitespace", temp_show_whitespace_, 4, ED_WSPC_Y  });
        g.spinners.push_back  ({ "Tab Size",        temp_indent_width_,  1, 16, 4, ED_TABSZ_Y });
        addGroup(std::move(g));
    }

    // ── Group 2: Display tab — checkbox section ───────────────────────────────
    {
        FocusGroup g;
        g.title = " Display "; g.hotkey = '\0';
        g.box_x = 2; g.box_y = CONTENT_Y; g.box_w = INNER_W; g.box_h = CONTENT_H;
        g.draw_widgets_manually = true;
        g.checkboxes.push_back({ "Show Line Numbers", temp_show_line_numbers_, 4, DI_LNUM_Y });
        addGroup(std::move(g));
    }

    // ── Group 3: Display tab — Syntax Highlighting radiolist ──────────────────
    {
        static std::vector<std::string> hl_items{ "None", "Basic", "Advanced (Clang)" };
        FocusGroup g;
        g.title = " Display "; g.hotkey = '\0';
        g.box_x = 2; g.box_y = CONTENT_Y; g.box_w = INNER_W; g.box_h = CONTENT_H;
        g.draw_widgets_manually = true;
        g.radiolists.push_back({ hl_items, temp_syntax_highlight_, temp_syntax_hl_cursor_,
                                 4, DI_SYNH_Y, 3 });
        addGroup(std::move(g));
    }

    // ── Group 4: Colors tab ───────────────────────────────────────────────────
    {
        FocusGroup g;
        g.title = " Colors "; g.hotkey = '\0';
        g.box_x = 2; g.box_y = CONTENT_Y; g.box_w = INNER_W; g.box_h = CONTENT_H;
        g.draw_widgets_manually = true;
        g.radiolists.push_back({ themes_, temp_theme_selected_, temp_theme_cursor_,
                                 4, CL_LIST_Y, CL_LIST_ROWS });
        addGroup(std::move(g));
    }

    // ── Button row ────────────────────────────────────────────────────────────
    static constexpr int BTN_SAVE_X   = 16;
    static constexpr int BTN_APPLY_X  = BTN_SAVE_X  + 7 + 2;
    static constexpr int BTN_CANCEL_X = BTN_APPLY_X + 8 + 2;

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
                    return HandleResult::CONTINUE;
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

    setGroupFocus(GRP_TABS);
    setGroupBtnFocus(0);
}

// ── onDraw() ──────────────────────────────────────────────────────────────────

void SettingsDialog::onDraw(Renderer& renderer, int sx, int sy)
{
    int active = groups()[GRP_TABS].tabcontrols[0].activeTab();

    // Expose only the active tab's content group(s) for mouse hit-testing;
    // zero-out inactive groups so they never intercept clicks.
    auto setBox = [&](int g, bool expose) {
        if (expose) {
            groups()[g].box_x = 2; groups()[g].box_y = CONTENT_Y;
            groups()[g].box_w = INNER_W; groups()[g].box_h = CONTENT_H;
        } else {
            groups()[g].box_w = 0; groups()[g].box_h = 0;
        }
    };
    setBox(GRP_EDITING,    active == 0);
    setBox(GRP_DISPLAY_CB, active == 1);
    setBox(GRP_DISPLAY_HL, active == 1);
    setBox(GRP_COLORS,     active == 2);

    // Draw widgets for the active tab manually
    switch (active) {
    case 0: {   // ── Editing ──────────────────────────────────────────────────
        bool focused = (getFocusedGroup() == GRP_EDITING);
        auto& g = groups()[GRP_EDITING];
        int item = 0;
        for (auto& cb : g.checkboxes)
            cb.draw(renderer, sx, sy, focused && g.inner_focus == item++);
        for (auto& sp : g.spinners)
            sp.draw(renderer, sx, sy, focused && g.inner_focus == item++);
        break;
    }
    case 1: {   // ── Display ──────────────────────────────────────────────────
        // Checkbox sub-group
        {
            bool focused = (getFocusedGroup() == GRP_DISPLAY_CB);
            auto& g = groups()[GRP_DISPLAY_CB];
            int item = 0;
            for (auto& cb : g.checkboxes)
                cb.draw(renderer, sx, sy, focused && g.inner_focus == item++);
        }
        // Label + radiolist sub-group
        renderer.drawText(sx + 4, sy + DI_SYNH_LY, "Syntax Highlighting:",
                          Renderer::CP_DIALOG);
        {
            bool focused = (getFocusedGroup() == GRP_DISPLAY_HL);
            auto& g = groups()[GRP_DISPLAY_HL];
            for (auto& rl : g.radiolists)
                rl.draw(renderer, sx, sy, focused);
        }
        break;
    }
    case 2: {   // ── Colors ───────────────────────────────────────────────────
        bool focused = (getFocusedGroup() == GRP_COLORS);
        auto& g = groups()[GRP_COLORS];
        for (auto& rl : g.radiolists)
            rl.draw(renderer, sx, sy, focused);
        break;
    }
    }
}

// ── onTab() ───────────────────────────────────────────────────────────────────

bool SettingsDialog::onTab(bool forward)
{
    int active = groups()[GRP_TABS].tabcontrols[0].activeTab();
    int cur    = getFocusedGroup();

    if (forward) {
        if (cur == GRP_TABS) {
            if (active == 0) { setGroupFocus(GRP_EDITING);    return true; }
            if (active == 1) { setGroupFocus(GRP_DISPLAY_CB); return true; }
            if (active == 2) { setGroupFocus(GRP_COLORS);     return true; }
        }
        if (cur == GRP_DISPLAY_CB) { setGroupFocus(GRP_DISPLAY_HL); return true; }
        if (cur == GRP_EDITING || cur == GRP_DISPLAY_HL || cur == GRP_COLORS) {
            setGroupFocus(groupCount()); setGroupBtnFocus(0);  return true;
        }
    } else {
        if (inButtonRow()) {
            if (active == 0) { setGroupFocus(GRP_EDITING);    return true; }
            if (active == 1) { setGroupFocus(GRP_DISPLAY_HL); return true; }
            if (active == 2) { setGroupFocus(GRP_COLORS);     return true; }
        }
        if (cur == GRP_DISPLAY_HL) { setGroupFocus(GRP_DISPLAY_CB); return true; }
        if (cur == GRP_EDITING || cur == GRP_DISPLAY_CB || cur == GRP_COLORS) {
            setGroupFocus(GRP_TABS); return true;
        }
    }
    return false;
}

// ── applySettings() ───────────────────────────────────────────────────────────

void SettingsDialog::applySettings()
{
    config_.smart_indentation = temp_smart_indent_;
    config_.indentation_width = temp_indent_width_;
    config_.show_whitespace   = temp_show_whitespace_;
    config_.show_line_numbers = temp_show_line_numbers_;
    config_.syntax_highlight  = temp_syntax_highlight_;
    config_.color_scheme_name = themes_[temp_theme_selected_];
    renderer_.loadColors(configManager_.loadThemes()[config_.color_scheme_name]);
}
