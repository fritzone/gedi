#include "SettingsDialog.h"
#include "Config.h"
#include "ConfigManager.h"
#include "utils.h"
#include <algorithm>

static constexpr int W          = 60;
static constexpr int H          = 22;   // fits an 80x25 screen (was 28)
static constexpr int INNER_W    = W - 4;
static constexpr int TAB_Y      = 2;
static constexpr int CONTENT_Y  = 4;
static constexpr int CONTENT_H  = H - 7;   // = 15 (rows 4..18)
static constexpr int BTN_Y      = H - 3;   // = 19

// Widget y-positions inside the content box (dialog-relative)
// Tab 0 – Editing. innerCount order is all checkboxes then the spinner.
static constexpr int ED_SMART_Y  = CONTENT_Y + 2;   // inner_focus 0
static constexpr int ED_WSPC_Y   = CONTENT_Y + 3;   // inner_focus 1
static constexpr int ED_USETAB_Y = CONTENT_Y + 4;   // inner_focus 2
static constexpr int ED_TABSZ_Y  = CONTENT_Y + 6;   // spinner (blank gap above)
#ifdef GEDI_GUI
// "Text Rendering" label + radiolist (its own focus group; graphical only).
static constexpr int ED_RENDER_LY   = CONTENT_Y + 8;
static constexpr int ED_RENDER_Y    = CONTENT_Y + 9;
static constexpr int ED_RENDER_ROWS = 3;
#endif

// Tab 1 – Display
static constexpr int DI_LNUM_Y  = CONTENT_Y + 2;   // Show Line Numbers checkbox
#ifdef GEDI_GUI
static constexpr int DI_ROUND_Y  = CONTENT_Y + 3;  // Rounded Corners checkbox (graphical only)
static constexpr int DI_INLINE_Y = CONTENT_Y + 4;  // Inline Error Text checkbox
static constexpr int DI_SYNH_LY = CONTENT_Y + 6;   // "Syntax Highlighting:" label row
static constexpr int DI_SYNH_Y  = CONTENT_Y + 7;   // radiolist start
#else
static constexpr int DI_INLINE_Y = CONTENT_Y + 3;  // Inline Error Text checkbox
static constexpr int DI_SYNH_LY = CONTENT_Y + 5;
static constexpr int DI_SYNH_Y  = CONTENT_Y + 6;
#endif
#ifdef GEDI_GUI
// Editor-font listbox on the right side of the Display tab.
static constexpr int DI_FONT_X    = 31;
static constexpr int DI_FONT_W    = 24;                // listbox width (incl. scrollbar)
static constexpr int DI_FONT_LY   = CONTENT_Y + 1;     // "Editor Font:" label
static constexpr int DI_FONT_Y    = CONTENT_Y + 2;     // first list row
static constexpr int DI_FONT_ROWS = CONTENT_H - 3;     // visible rows
#endif

// Tab 2 – Colors
static constexpr int CL_LIST_Y    = CONTENT_Y + 2;
static constexpr int CL_LIST_ROWS = CONTENT_H - 4; // = 11

SettingsDialog::SettingsDialog(Renderer& renderer, Config& config,
                               ConfigManager& configManager)
    : DialogBase("Editor Settings", W, H)
    , renderer_(renderer)
    , config_(config), configManager_(configManager)
    , temp_smart_indent_    (config.smart_indentation)
    , temp_indent_width_    (config.indentation_width)
    , temp_use_tab_char_    (config.use_tab_character)
    , temp_show_whitespace_ (config.show_whitespace)
    , temp_render_mode_     (std::max(0, std::min(2, config.text_render_mode)))
    , temp_render_cursor_   (std::max(0, std::min(2, config.text_render_mode)))
    , temp_show_line_numbers_(config.show_line_numbers)
    , temp_inline_diag_     (config.show_inline_diagnostics)
    , temp_rounded_corners_ (config.rounded_corners)
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

#ifdef GEDI_GUI
    for (const auto& f : listEditorFonts()) {
        font_names_.push_back(f.first);
        font_paths_.push_back(f.second);
        font_indices_.push_back(gui_register_font(f.second.empty() ? nullptr
                                                                    : f.second.c_str()));
    }
    for (int i = 0; i < (int)font_names_.size(); ++i)
        if (font_names_[i] == config.editor_font) {
            temp_font_selected_ = temp_font_cursor_ = i;
            break;
        }
#endif
}

void SettingsDialog::show(Renderer& renderer, Config& config,
                          ConfigManager& configManager,
                          std::function<void()> on_background_refresh)
{
    SettingsDialog dlg(renderer, config, configManager);
    if (on_background_refresh)
        dlg.setBackgroundRefresh(std::move(on_background_refresh));
    dlg.run(renderer);
}

void SettingsDialog::onInit()
{
    // Group 0: Tab bar
    {
        FocusGroup g;
        g.title = ""; g.hotkey = '\0';
        g.box_w = 0; g.box_h = 0; g.box_x = 0; g.box_y = 0;
        g.tabcontrols.push_back(TabControl({"Editing", "Display", "Colors"}, 2, TAB_Y, INNER_W));
        addGroup(std::move(g));
    }

    //  Group 1: Editing tab
    // innerCount order: checkboxes[0], checkboxes[1], spinners[0]
    {
        FocusGroup g;
        g.title = " Editing "; g.hotkey = '\0';
        g.box_x = 2; g.box_y = CONTENT_Y; g.box_w = INNER_W; g.box_h = CONTENT_H;
        g.draw_widgets_manually = true;
        g.checkboxes.push_back({ "Smart Indent",     temp_smart_indent_,    4, ED_SMART_Y  });
        g.checkboxes.push_back({ "Show Whitespace",  temp_show_whitespace_, 4, ED_WSPC_Y   });
        g.checkboxes.push_back({ "Use Tab Character", temp_use_tab_char_,   4, ED_USETAB_Y });
        g.spinners.push_back  ({ "Tab Size",         temp_indent_width_,  1, 16, 4, ED_TABSZ_Y });
        addGroup(std::move(g));
    }

    //  Group 2: Display tab - checkbox section
    {
        FocusGroup g;
        g.title = " Display "; g.hotkey = '\0';
        g.box_x = 2; g.box_y = CONTENT_Y; g.box_w = INNER_W; g.box_h = CONTENT_H;
        g.draw_widgets_manually = true;
        g.checkboxes.push_back({ "Show Line Numbers", temp_show_line_numbers_, 4, DI_LNUM_Y });
#ifdef GEDI_GUI
        g.checkboxes.push_back({ "Rounded Corners", temp_rounded_corners_, 4, DI_ROUND_Y });
#endif
        g.checkboxes.push_back({ "Inline Error Text", temp_inline_diag_, 4, DI_INLINE_Y });
        addGroup(std::move(g));
    }

    //  Group 3: Display tab - Syntax Highlighting radiolist
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

    //  Group 4: Colors tab
    {
        FocusGroup g;
        g.title = " Colors "; g.hotkey = '\0';
        g.box_x = 2; g.box_y = CONTENT_Y; g.box_w = INNER_W; g.box_h = CONTENT_H;
        g.draw_widgets_manually = true;
        g.radiolists.push_back({ themes_, temp_theme_selected_, temp_theme_cursor_,
                                 4, CL_LIST_Y, CL_LIST_ROWS });
        addGroup(std::move(g));
    }

#ifdef GEDI_GUI
    //  Group 5: Display tab - editor font radiolist (right side)
    {
        FocusGroup g;
        g.title = " Display "; g.hotkey = '\0';
        g.box_x = 2; g.box_y = CONTENT_Y; g.box_w = INNER_W; g.box_h = CONTENT_H;
        g.draw_widgets_manually = true;
        g.radiolists.push_back({ font_names_, temp_font_selected_, temp_font_cursor_,
                                 DI_FONT_X, DI_FONT_Y, DI_FONT_ROWS });
        addGroup(std::move(g));
    }

    //  Group 6: Editing tab - Text Rendering radiolist
    {
        static std::vector<std::string> render_items{ "Pixelated", "Smooth", "Sharp" };
        FocusGroup g;
        g.title = " Editing "; g.hotkey = '\0';
        g.box_x = 2; g.box_y = CONTENT_Y; g.box_w = INNER_W; g.box_h = CONTENT_H;
        g.draw_widgets_manually = true;
        g.radiolists.push_back({ render_items, temp_render_mode_, temp_render_cursor_,
                                 4, ED_RENDER_Y, ED_RENDER_ROWS });
        addGroup(std::move(g));
    }
#endif

    //  Button row
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
                .label = " &Close ",
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

//  onDraw()

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
#ifdef GEDI_GUI
    setBox(GRP_DISPLAY_FONT,    active == 1);
    setBox(GRP_EDITING_RENDER,  active == 0);
#endif

    // Draw widgets for the active tab manually
    switch (active) {
    case 0: {   //  Editing
        bool focused = (getFocusedGroup() == GRP_EDITING);
        auto& g = groups()[GRP_EDITING];
        int item = 0;
        for (auto& cb : g.checkboxes)
            cb.draw(renderer, sx, sy, focused && g.inner_focus == item++);
        for (auto& sp : g.spinners)
            sp.draw(renderer, sx, sy, focused && g.inner_focus == item++);
#ifdef GEDI_GUI
        // Text Rendering label + radiolist (own focus group).
        renderer.drawText(sx + 4, sy + ED_RENDER_LY, "Text Rendering:",
                          Renderer::CP_DIALOG);
        {
            bool rfocused = (getFocusedGroup() == GRP_EDITING_RENDER);
            for (auto& rl : groups()[GRP_EDITING_RENDER].radiolists)
                rl.draw(renderer, sx, sy, rfocused);
        }
#endif
        break;
    }
    case 1: {   //  Display
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
#ifdef GEDI_GUI
        // Editor font listbox (right side). The RadioList in GRP_DISPLAY_FONT only
        // holds the state (cursor/scroll); it is drawn manually here as a proper
        // scrollable listbox, with every entry rendered in its own font.
        renderer.drawText(sx + DI_FONT_X, sy + DI_FONT_LY, "Editor Font:",
                          Renderer::CP_DIALOG);
        {
            bool focused = (getFocusedGroup() == GRP_DISPLAY_FONT);
            auto& rl = groups()[GRP_DISPLAY_FONT].radiolists[0];
            int total   = (int)font_names_.size();
            int rows    = DI_FONT_ROWS;
            int textw   = DI_FONT_W - 1;                 // leave 1 col for the scrollbar
            int top     = rl.scrollOffset();
            for (int i = 0; i < rows; ++i) {
                int yy  = sy + DI_FONT_Y + i;
                int idx = top + i;
                bool cursor = (idx == rl.cursor_idx);
                int  bg = cursor ? Renderer::CP_LIST_SELECTED : Renderer::CP_LIST_BOX;
                // row background
                renderer.drawText(sx + DI_FONT_X, yy, std::string(textw, ' '), bg);
                if (idx >= total) continue;
                std::string name = font_names_[idx];
                if ((int)name.size() > textw - 1) name = name.substr(0, textw - 1);
                // Render the name in this font's own glyphs (A_FONT id); "Default"
                // (index 0) renders in the standard font.
                renderer.drawText(sx + DI_FONT_X + 1, yy, name, bg, A_FONT(font_indices_[idx]));
            }
            // Scrollbar
            int sbx = sx + DI_FONT_X + DI_FONT_W - 1;
            if (total > rows) {
                renderer.drawText(sbx, sy + DI_FONT_Y, "\xe2\x86\x91",
                                  focused ? Renderer::CP_HIGHLIGHT : Renderer::CP_LIST_BOX);   // ↑
                renderer.drawText(sbx, sy + DI_FONT_Y + rows - 1, "\xe2\x86\x93",
                                  focused ? Renderer::CP_HIGHLIGHT : Renderer::CP_LIST_BOX);   // ↓
                int track = rows - 2;
                if (track > 0) {
                    float frac = (total > 1) ? (float)rl.cursor_idx / (total - 1) : 0.f;
                    int thumb = (int)(frac * (track - 1) + 0.5f);
                    renderer.drawText(sbx, sy + DI_FONT_Y + 1 + thumb, "\xe2\x96\x88",
                                      Renderer::CP_HIGHLIGHT);   // █
                }
            }
        }
#endif
        break;
    }
    case 2: {   //  Colors
        bool focused = (getFocusedGroup() == GRP_COLORS);
        auto& g = groups()[GRP_COLORS];
        for (auto& rl : g.radiolists)
            rl.draw(renderer, sx, sy, focused);
        break;
    }
    }
}

//  onTab()

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
#ifdef GEDI_GUI
        if (cur == GRP_DISPLAY_HL) { setGroupFocus(GRP_DISPLAY_FONT); return true; }
        if (cur == GRP_EDITING)    { setGroupFocus(GRP_EDITING_RENDER); return true; }
        if (cur == GRP_EDITING_RENDER || cur == GRP_DISPLAY_FONT || cur == GRP_COLORS) {
            setGroupFocus(groupCount()); setGroupBtnFocus(0);  return true;
        }
#else
        if (cur == GRP_EDITING || cur == GRP_DISPLAY_HL || cur == GRP_COLORS) {
            setGroupFocus(groupCount()); setGroupBtnFocus(0);  return true;
        }
#endif
    } else {
        if (inButtonRow()) {
#ifdef GEDI_GUI
            if (active == 0) { setGroupFocus(GRP_EDITING_RENDER); return true; }
            if (active == 1) { setGroupFocus(GRP_DISPLAY_FONT); return true; }
#else
            if (active == 0) { setGroupFocus(GRP_EDITING);    return true; }
            if (active == 1) { setGroupFocus(GRP_DISPLAY_HL); return true; }
#endif
            if (active == 2) { setGroupFocus(GRP_COLORS);     return true; }
        }
#ifdef GEDI_GUI
        if (cur == GRP_DISPLAY_FONT) { setGroupFocus(GRP_DISPLAY_HL); return true; }
        if (cur == GRP_EDITING_RENDER) { setGroupFocus(GRP_EDITING); return true; }
#endif
        if (cur == GRP_DISPLAY_HL) { setGroupFocus(GRP_DISPLAY_CB); return true; }
        if (cur == GRP_EDITING || cur == GRP_DISPLAY_CB || cur == GRP_COLORS) {
            setGroupFocus(GRP_TABS); return true;
        }
    }
    return false;
}

//  applySettings()

void SettingsDialog::applySettings()
{
    config_.smart_indentation = temp_smart_indent_;
    config_.indentation_width = temp_indent_width_;
    config_.use_tab_character = temp_use_tab_char_;
    config_.show_whitespace   = temp_show_whitespace_;
#ifdef GEDI_GUI
    config_.text_render_mode = temp_render_mode_;
    gui_set_render_mode(config_.text_render_mode);          // apply live
    config_.rounded_corners = temp_rounded_corners_;
    gui_set_rounded_corners(config_.rounded_corners ? 1 : 0);
    if (temp_font_cursor_ >= 0 && temp_font_cursor_ < (int)font_names_.size()) {
        config_.editor_font = font_names_[temp_font_cursor_];
        const std::string& path = font_paths_[temp_font_cursor_];
        gui_set_font(path.empty() ? nullptr : path.c_str());
    }
#endif
    config_.show_line_numbers = temp_show_line_numbers_;
    config_.show_inline_diagnostics = temp_inline_diag_;
    config_.syntax_highlight  = temp_syntax_highlight_;
    config_.color_scheme_name = themes_[temp_theme_selected_];
    renderer_.loadColors(configManager_.loadThemes()[config_.color_scheme_name]);
}
