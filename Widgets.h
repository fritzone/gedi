#pragma once
#include <ncurses.h>
#include "Renderer.h"
#include <string>
#include <vector>
#include <functional>

// ═══════════════════════════════════════════════════════════════════════════════
// Widgets.h
//
// Widget catalogue:
//   Button      — push-button with & hotkey
//   ButtonRow   — horizontal row of Buttons (one Tab stop)
//   CheckBox    — [X] / [ ] boolean toggle
//   Spinner     — < N > bounded integer
//   RadioList   — scrollable (•)/( ) single-selection list
//   ComboBox    — [ item ] left/right cycling selector
//   TabControl  — horizontal tab bar with separator line
//   OptionList  — scrollable mixed checkbox/radio list built from an Option array
//   FocusGroup  — titled box containing any mix of the above
// ═══════════════════════════════════════════════════════════════════════════════

enum class HandleResult { CONTINUE, CLOSE };

// ── Button ────────────────────────────────────────────────────────────────────
struct Button {
    std::string label;
    int         x, y;
    std::function<HandleResult()> on_activate;

    char hotkey() const noexcept {
        for (std::size_t i = 0; i + 1 < label.size(); ++i)
            if (label[i] == '&')
                return static_cast<char>(
                    std::tolower(static_cast<unsigned char>(label[i + 1])));
        return '\0';
    }

    void draw(Renderer& renderer, int startx, int starty,
              bool selected, bool pressed) const {
        renderer.drawButton(startx + x, starty + y, label, selected, pressed);
    }
};

// ── ButtonRow ─────────────────────────────────────────────────────────────────
struct ButtonRow {
    std::vector<Button> buttons;
    int inner_focus = 0;

    void draw(Renderer& renderer, int startx, int starty,
              bool row_focused, bool pressed) const {
        for (int i = 0; i < (int)buttons.size(); ++i) {
            bool sel  = row_focused && (i == inner_focus);
            bool pres = pressed && sel;
            buttons[i].draw(renderer, startx, starty, sel, pres);
        }
    }

    bool handleNavKey(wint_t ch) {
        if (ch == KEY_LEFT  && inner_focus > 0)
        { --inner_focus; return true; }
        if (ch == KEY_RIGHT && inner_focus < (int)buttons.size() - 1)
        { ++inner_focus; return true; }
        return false;
    }

    Button* focusedButton() {
        if (inner_focus >= 0 && inner_focus < (int)buttons.size())
            return &buttons[inner_focus];
        return nullptr;
    }

    Button* findByHotkey(char lower) {
        for (auto& btn : buttons)
            if (btn.hotkey() == lower) return &btn;
        return nullptr;
    }
};

// ── CheckBox ──────────────────────────────────────────────────────────────────
struct CheckBox {
    std::string label;
    bool&       value;
    int         x, y;

    void draw(Renderer& renderer, int startx, int starty, bool focused) const {
        renderer.drawText(startx + x, starty + y,
                          value ? "[X]" : "[ ]",
                          focused ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
        renderer.drawText(startx + x + 4, starty + y, label, Renderer::CP_DIALOG);
    }

    bool handleKey(wint_t ch) {
        if (ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13)
        { value = !value; return true; }
        return false;
    }
};

// ── Spinner ───────────────────────────────────────────────────────────────────
struct Spinner {
    std::string label;
    int&        value;
    int         min_val, max_val;
    int         x, y;

    void draw(Renderer& renderer, int startx, int starty, bool focused) const {
        renderer.drawText(startx + x, starty + y,
                          "< " + std::to_string(value) + " >",
                          focused ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
        renderer.drawText(startx + x + 8, starty + y, label, Renderer::CP_DIALOG);
    }

    bool handleKey(wint_t ch) {
        if (ch == KEY_LEFT  && value > min_val) { --value; return true; }
        if (ch == KEY_RIGHT && value < max_val) { ++value; return true; }
        return false;
    }
};

// ── RadioList ─────────────────────────────────────────────────────────────────
struct RadioList {
    std::vector<std::string> items;
    int&  selected_idx;
    int&  cursor_idx;
    int   x, y;
    int   visible_rows;

    void draw(Renderer& renderer, int startx, int starty, bool group_focused) const {
        int top = scrollOffset();
        for (int i = 0; i < visible_rows; ++i) {
            int idx = top + i;
            if (idx >= (int)items.size()) break;
            bool item_focused = group_focused && (idx == cursor_idx);
            renderer.drawText(startx + x, starty + y + i,
                              idx == selected_idx ? "(•)" : "( )",
                              item_focused ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
            renderer.drawText(startx + x + 4, starty + y + i, items[idx], Renderer::CP_DIALOG);
        }
    }

    bool handleKey(wint_t ch) {
        if (ch == KEY_UP   && cursor_idx > 0)                        { --cursor_idx; return true; }
        if (ch == KEY_DOWN && cursor_idx < (int)items.size() - 1)    { ++cursor_idx; return true; }
        if (ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13)    { selected_idx = cursor_idx; return true; }
        return false;
    }

    int scrollOffset() const {
        return (cursor_idx >= visible_rows) ? cursor_idx - visible_rows + 1 : 0;
    }
};

// ── ComboBox ──────────────────────────────────────────────────────────────────
// Displays the selected item between [ ] brackets.
// Left/Up — previous item.   Right/Down/Space — next item (Space wraps).
struct ComboBox {
    std::vector<std::string> items;
    int  selected_idx = 0;
    int  x, y, w;        // position and total width (including brackets)

    ComboBox() = default;
    ComboBox(const std::vector<std::string>& i, int sel, int x_, int y_, int w_)
        : items(i), selected_idx(sel), x(x_), y(y_), w(w_)
    {
        if (selected_idx < 0) selected_idx = 0;
        if (!items.empty() && selected_idx >= (int)items.size())
            selected_idx = (int)items.size() - 1;
    }

    void draw(Renderer& renderer, int startx, int starty, bool focused) const {
        if (items.empty()) return;
        std::string text = items[selected_idx];
        int inner_w = w - 4;
        if ((int)text.length() > inner_w) text = text.substr(0, inner_w);
        int color = focused ? Renderer::CP_MENU_SELECTED : Renderer::CP_LIST_BOX;
        renderer.drawText(startx + x,         starty + y, "[ ",  Renderer::CP_DIALOG);
        renderer.drawText(startx + x + 2,     starty + y,
                          text + std::string(inner_w - text.length(), ' '), color);
        renderer.drawText(startx + x + w - 2, starty + y, " ]",  Renderer::CP_DIALOG);
    }

    bool handleKey(wint_t ch) {
        if (items.empty()) return false;
        if (ch == KEY_LEFT || ch == KEY_UP) {
            if (selected_idx > 0) { --selected_idx; return true; }
        } else if (ch == KEY_RIGHT || ch == KEY_DOWN) {
            if (selected_idx < (int)items.size() - 1) { ++selected_idx; return true; }
        } else if (ch == ' ') {
            selected_idx = (items.size() > 1)
            ? (selected_idx + 1) % (int)items.size() : 0;
            return true;
        }
        return false;
    }

    const std::string& selectedText() const {
        static const std::string empty;
        return items.empty() ? empty : items[selected_idx];
    }
};

// ── TabControl ────────────────────────────────────────────────────────────────
// Horizontal tab bar. Left/Right switch tabs.
// Draws the tab labels + a separator line below them.
// The active tab index is publicly readable via activeTab().
struct TabControl {
    std::vector<std::string> tab_names;
    int active_tab = 0;
    int x, y, w;    // top-left and total width for the separator line

    TabControl() = default;
    TabControl(const std::vector<std::string>& names, int x_, int y_, int w_,
               int initial = 0)
        : tab_names(names), active_tab(initial), x(x_), y(y_), w(w_)
    {
        if (active_tab < 0) active_tab = 0;
        if (!tab_names.empty() && active_tab >= (int)tab_names.size())
            active_tab = (int)tab_names.size() - 1;
    }

    void draw(Renderer& renderer, int startx, int starty, bool focused) const {
        int cur_x = startx + x;
        for (int i = 0; i < (int)tab_names.size(); ++i) {
            std::string label = " " + tab_names[i] + " ";
            int color = (i == active_tab)
                            ? (focused ? Renderer::CP_MENU_SELECTED : Renderer::CP_HIGHLIGHT)
                            : Renderer::CP_DIALOG;
            int style = (i == active_tab) ? A_BOLD : 0;
            renderer.drawText(cur_x, starty + y, label, color, style);
            cur_x += (int)label.size() + 1;
        }
        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        mvwhline(stdscr, starty + y + 1, startx + x, ACS_HLINE, w);
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
    }

    bool handleKey(wint_t ch) {
        if (ch == KEY_LEFT && active_tab > 0)
        { --active_tab; return true; }
        if (ch == KEY_RIGHT && active_tab < (int)tab_names.size() - 1)
        { ++active_tab; return true; }
        return false;
    }

    int activeTab() const noexcept { return active_tab; }
};

// ── OptionList ────────────────────────────────────────────────────────────────
// A scrollable list of mixed checkboxes and radio-buttons, organised into
// named groups. This is the widget used by CompileOptionsDialog.
//
// Each Option references either a bool* (checkbox) or an int* (radio button).
// Group headers are inserted automatically when the group name changes.
struct OptionList {
    struct Option {
        std::string label;
        std::string group;
        bool  is_radio   = false;
        int   radio_val  = 0;
        bool* b_val      = nullptr;   // non-null for checkboxes
        int*  i_val      = nullptr;   // non-null for radio buttons
    };

    std::vector<Option> options;  // set by the subclass each time the tab changes
    int cursor    = 0;            // currently highlighted option index
    int top_row   = 0;            // scroll offset (option index of first visible row)
    int x, y;                     // position relative to dialog top-left
    int visible_rows;

    // ── Flat row view (group headers interspersed) ────────────────────────────
    struct Row { bool is_group; std::string text; int opt_idx; };

    std::vector<Row> buildRows() const {
        std::vector<Row> rows;
        std::string last_group;
        for (int i = 0; i < (int)options.size(); ++i) {
            if (options[i].group != last_group) {
                last_group = options[i].group;
                rows.push_back({ true, "- " + last_group, -1 });
            }
            bool val = options[i].is_radio
                           ? (*options[i].i_val == options[i].radio_val)
                           : *options[i].b_val;
            std::string mark = options[i].is_radio ? (val ? "(•)" : "( )") : (val ? "[X]" : "[ ]");
            rows.push_back({ false, mark + " " + options[i].label, i });
        }
        return rows;
    }

    void draw(Renderer& renderer, int startx, int starty, bool focused) const {
        auto rows = buildRows();

        // Find the display row of the cursor option and scroll to keep it visible
        int selected_row = -1;
        for (int i = 0; i < (int)rows.size(); ++i)
            if (!rows[i].is_group && rows[i].opt_idx == cursor) { selected_row = i; break; }

        // (const-cast trick: top_row is logically mutable scroll state)
        int& top = const_cast<int&>(top_row);
        if (selected_row >= 0) {
            if (selected_row < top) top = selected_row;
            if (selected_row >= top + visible_rows) top = selected_row - visible_rows + 1;
        }

        for (int i = 0; i < visible_rows && (top + i) < (int)rows.size(); ++i) {
            const auto& row = rows[top + i];
            if (row.is_group) {
                renderer.drawText(startx + x, starty + y + i, row.text,
                                  Renderer::CP_DIALOG, A_BOLD);
            } else {
                bool sel = focused && (row.opt_idx == cursor);
                renderer.drawText(startx + x + 2, starty + y + i, row.text,
                                  sel ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
            }
        }
    }

    bool handleKey(wint_t ch) {
        if (ch == KEY_UP && cursor > 0)
        { --cursor; return true; }
        if (ch == KEY_DOWN && cursor < (int)options.size() - 1)
        { ++cursor; return true; }
        if ((ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13)
            && cursor < (int)options.size()) {
            auto& opt = options[cursor];
            if (opt.is_radio) *opt.i_val = opt.radio_val;
            else              *opt.b_val = !*opt.b_val;
            return true;
        }
        return false;
    }

    // Reset cursor and scroll when the tab changes
    void reset() { cursor = 0; top_row = 0; }
};

// ── FocusGroup ────────────────────────────────────────────────────────────────
struct FocusGroup {
    std::string title;
    char        hotkey = '\0';
    int         box_x, box_y, box_w, box_h;

    std::vector<CheckBox>   checkboxes;
    std::vector<Spinner>    spinners;
    std::vector<RadioList>  radiolists;
    std::vector<ComboBox>   comboboxes;
    std::vector<TabControl> tabcontrols;
    std::vector<OptionList> optionlists;

    // For groups with a raw text input (like "Optional flags"), the subclass
    // stores the buffer reference here and the base class handles typing/backspace.
    std::string* text_buffer = nullptr;   // nullptr = no raw text input

    int inner_focus = 0;

    // Total focusable items: cb + sp + combo + tab + optionlist
    // (RadioList is one unit; text_buffer has no separate focus — it's the whole group)
    int innerCount() const {
        if (text_buffer) return 1;   // the whole group is one text field
        return static_cast<int>(
            checkboxes.size() + spinners.size() +
            comboboxes.size() + tabcontrols.size() +
            optionlists.size());
    }

    void draw(Renderer& renderer, int startx, int starty, bool focused) const {
        if (box_w > 0 && box_h > 0)
            renderer.drawBoxWithTitle(startx + box_x, starty + box_y,
                                      box_w, box_h,
                                      Renderer::CP_DIALOG, Renderer::SINGLE,
                                      title, Renderer::CP_DIALOG,
                                      focused ? A_BOLD : 0);

        // text_buffer group: just draw the input field, no inner items
        if (text_buffer) {
            // The drawing is done by the dialog's onDraw — nothing extra here
            return;
        }

        int item = 0;
        for (const auto& cb : checkboxes)
            cb.draw(renderer, startx, starty, focused && inner_focus == item++);
        for (const auto& sp : spinners)
            sp.draw(renderer, startx, starty, focused && inner_focus == item++);
        for (auto& combo : comboboxes)
            combo.draw(renderer, startx, starty, focused && inner_focus == item++);
        for (auto& tab : tabcontrols)
            tab.draw(renderer, startx, starty, focused && inner_focus == item++);
        for (auto& ol : optionlists)
            ol.draw(renderer, startx, starty, focused);  // OptionList = one unit
        for (const auto& rl : radiolists)
            rl.draw(renderer, startx, starty, focused);
    }

    bool handleKey(wint_t ch) {
        // Text buffer group: typing/backspace handled by DialogBase directly
        if (text_buffer) return false;

        int count = innerCount();
        // Up/Down move inner focus only when there are multiple focusable items
        // and no OptionList (OptionList absorbs Up/Down internally)
        bool has_optionlist = !optionlists.empty();
        if (!has_optionlist && count > 1) {
            if (ch == KEY_UP   && inner_focus > 0)        { --inner_focus; return true; }
            if (ch == KEY_DOWN && inner_focus < count - 1) { ++inner_focus; return true; }
        }

        int item = 0;
        for (auto& cb : checkboxes)
            if (inner_focus == item++) return cb.handleKey(ch);
        for (auto& sp : spinners)
            if (inner_focus == item++) return sp.handleKey(ch);
        for (auto& combo : comboboxes)
            if (inner_focus == item++) return combo.handleKey(ch);
        for (auto& tab : tabcontrols)
            if (inner_focus == item++) return tab.handleKey(ch);
        for (auto& ol : optionlists)
            return ol.handleKey(ch);
        for (auto& rl : radiolists)
            return rl.handleKey(ch);
        return false;
    }
};
