#pragma once
#include "curses_compat.h"
#include "Renderer.h"
#include "ScrollBar.h"
#include <algorithm>
#include <string>
#include <vector>
#include <functional>

// Ctrl+Down key code - pinned by Renderer::Renderer() via define_key(tigetstr("kDN5"), 525)
static constexpr wint_t KEY_CTRL_DOWN = 525;

// ═══════════════════════════════════════════════════════════════════════════════
// Widgets.h
//
// Widget catalogue:
//   Button      - push-button with & hotkey
//   ButtonRow   - horizontal row of Buttons (one Tab stop)
//   CheckBox    - [X] / [ ] boolean toggle
//   Spinner     - < N > bounded integer
//   RadioList   - scrollable (•)/( ) single-selection list
//   ComboBox    - [ item ] left/right cycling selector
//   TabControl  - horizontal tab bar with separator line
//   OptionList  - scrollable mixed checkbox/radio list built from an Option array
//   FocusGroup  - titled box containing any mix of the above
// ═══════════════════════════════════════════════════════════════════════════════

enum class HandleResult { CONTINUE, CLOSE };

//  Button 
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

//  ButtonRow 
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

//  CheckBox 
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

//  Spinner 
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

//  RadioList 
struct RadioList {
    std::vector<std::string> items;
    int&  selected_idx;
    int&  cursor_idx;
    int   x, y;
    int   visible_rows;
    // Columns of the list; with one, a list longer than its rows draws a
    // scrollbar in the column after them.
    int   width = 0;
    // The first row shown: it follows the cursor when that moves, and moves
    // alone when the list is scrolled (scrollTo: the wheel, a scrollbar).
    mutable int top_row      = 0;
    mutable int seen_cursor_ = -1;
    // A list drawn with a scrollbar lays it out here and sets has_bar for the
    // frame; DialogBase then gives it clicks, drags and the wheel.
    mutable ScrollBar bar;
    mutable bool      has_bar = false;

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
        if (width > 0 && visible_rows >= 3 && (int)items.size() > visible_rows) {
            bar.layout(startx + x + width, starty + y, visible_rows, true,
                       (int)items.size(), visible_rows, top);
            has_bar = true;
            bar.draw(renderer, Renderer::CP_DIALOG);
        }
    }

    bool handleKey(wint_t ch) {
        if (ch == KEY_UP   && cursor_idx > 0)                        { --cursor_idx; return true; }
        if (ch == KEY_DOWN && cursor_idx < (int)items.size() - 1)    { ++cursor_idx; return true; }
        if (ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13)    { selected_idx = cursor_idx; return true; }
        return false;
    }

    int scrollOffset() const {
        if (cursor_idx != seen_cursor_) {
            seen_cursor_ = cursor_idx;
            if (cursor_idx < top_row) top_row = cursor_idx;
            if (cursor_idx >= top_row + visible_rows) top_row = cursor_idx - visible_rows + 1;
        }
        top_row = std::clamp(top_row, 0, std::max(0, (int)items.size() - visible_rows));
        return top_row;
    }
    void scrollTo(int top) {
        scrollOffset();
        top_row = std::clamp(top, 0, std::max(0, (int)items.size() - visible_rows));
    }
};

//  ComboBox
// The one combo box of the program: every dialog's combo is this widget, so
// they all look and behave alike.
//
// Closed:  [ Selected item            ▼ ]
//   Up / Down        select the previous / next item at once (its action, the
//                    owner's on_change, runs immediately)
//   Ctrl+Space       open the list (Ctrl+Down too)
//   mouse on  ▼      open the list
// Open (the list drops below, or above with open_up):
//   Up / Down / PgUp / PgDn / Home / End    move through the list
//   Enter or a click on an item             select it and close
//   mouse wheel, the scrollbar              scroll the list (see ScrollBar.h)
//   Esc, Ctrl+Space, Tab, a click elsewhere close without changing
//
// While a list is open, DialogBase sends it every key and click first (see
// s_open), so dialogs need no special code for that state; a dialog that draws
// its combo itself only has to hand it keys and clicks while it has focus.
struct ComboBox {
    std::vector<std::string> items;
    int  selected_idx    = 0;
    int  x = 0, y = 0, w = 12;  // position (dialog-relative) and total width, brackets included
    bool dropdown_open   = false;
    int  dropdown_cursor = 0;   // highlighted row of the open list
    int  dropdown_top    = 0;   // first visible row of the open list
    // Open the list ABOVE the field instead of below it - for a combo that sits
    // low in its window, so the list stays inside it.
    bool open_up         = false;
    // Runs whenever the selection changes: Up/Down, Enter in the list, a click.
    std::function<void(int)> on_change;

    static constexpr int MAX_DD_ROWS = 8;

    // The one combo whose list is open (only one can be). The dialog framework
    // routes input to it and draws its list last, over everything else.
    static inline ComboBox* s_open = nullptr;

    ComboBox() = default;
    ComboBox(const std::vector<std::string>& i, int sel, int x_, int y_, int w_)
        : items(i), selected_idx(sel), x(x_), y(y_), w(w_)
    {
        if (selected_idx < 0) selected_idx = 0;
        if (!items.empty() && selected_idx >= (int)items.size())
            selected_idx = (int)items.size() - 1;
    }
    ComboBox(const ComboBox&)            = default;
    ComboBox& operator=(const ComboBox& o) {
        if (this != &o) {
            if (s_open == this) s_open = nullptr;    // the list being replaced closes
            items = o.items; selected_idx = o.selected_idx; x = o.x; y = o.y; w = o.w;
            dropdown_open = false; dropdown_cursor = o.dropdown_cursor; dropdown_top = o.dropdown_top;
            open_up = o.open_up; on_change = o.on_change;
        }
        return *this;
    }
    ~ComboBox() { if (s_open == this) s_open = nullptr; }

    int  visibleRows() const { return std::min((int)items.size(), MAX_DD_ROWS); }

    void open() {
        if (items.empty()) return;
        if (s_open && s_open != this) s_open->close();
        dropdown_open   = true;
        dropdown_cursor = selected_idx;
        dropdown_top    = 0;
        keepCursorVisible();
        s_open = this;
    }
    void close() { dropdown_open = false; bar_.cancelDrag(); if (s_open == this) s_open = nullptr; }

    // Make `idx` the selection and run the owner's action when it changed.
    void select(int idx) {
        if (items.empty()) return;
        idx = std::clamp(idx, 0, (int)items.size() - 1);
        if (idx == selected_idx) return;
        selected_idx = idx;
        if (on_change) on_change(idx);
    }

    const std::string& selectedText() const {
        static const std::string empty;
        return items.empty() ? empty : items[selected_idx];
    }

    //  Drawing
    // "[ " + text + " ▼ ]": the text in the list colours (highlighted when the
    // combo has focus), the arrow marking where a click opens the list.
    void draw(Renderer& renderer, int startx, int starty, bool focused) const {
        if (items.empty()) return;
        int inner_w = w - 6;                      // "[ " (2) + text + " ▼ ]" (4)
        if (inner_w < 1) inner_w = 1;
        std::string text = items[selected_idx];
        if ((int)text.size() > inner_w)
            text = inner_w > 3 ? text.substr(0, inner_w - 3) + "..." : text.substr(0, inner_w);
        text += std::string(inner_w - (int)text.size(), ' ');
        const int field = focused ? Renderer::CP_MENU_SELECTED : Renderer::CP_LIST_BOX;
        renderer.drawText(startx + x,         starty + y, "[ ", Renderer::CP_DIALOG);
        renderer.drawText(startx + x + 2,     starty + y, text, field);
        renderer.drawText(startx + x + w - 4, starty + y, " ", field);
        renderer.drawText(startx + x + w - 3, starty + y,
                          dropdown_open ? "\xe2\x96\xb2" : "\xe2\x96\xbc",   // ▲ / ▼
                          field, A_BOLD);
        renderer.drawText(startx + x + w - 2, starty + y, " ]", Renderer::CP_DIALOG);
    }

    // The open list, framed, floating below (or above) the field; ▲ / ▼ on its
    // right edge when there is more above / below. Drawn by the framework after
    // everything else.
    void drawDropdown(Renderer& renderer, int startx, int starty) const {
        if (!dropdown_open || items.empty()) return;
        int sx, sy;
        listOrigin(startx, starty, sx, sy);
        const int visible = visibleRows();
        const int dh = visible + 2;

        if (!open_up) renderer.drawShadow(sx, sy, w, dh);
        renderer.drawBox(sx, sy, w, dh, Renderer::CP_DIALOG, Renderer::SINGLE);

        const int inner_w = w - 2;
        for (int i = 0; i < visible; ++i) {
            const int idx = dropdown_top + i;
            if (idx >= (int)items.size()) break;
            std::string text = " " + items[idx];
            if ((int)text.size() > inner_w) text = text.substr(0, inner_w);
            else                            text += std::string(inner_w - (int)text.size(), ' ');
            renderer.drawText(sx + 1, sy + 1 + i, text,
                              idx == dropdown_cursor ? Renderer::CP_LIST_SELECTED : Renderer::CP_LIST_BOX,
                              idx == selected_idx ? A_BOLD : A_NORMAL);
        }
        // A list longer than its window gets the scrollbar on the frame's
        // right edge.
        layoutBar(startx, starty);
        if (scrollable()) bar_.draw(renderer, Renderer::CP_DIALOG);
    }

    //  Scrolling
    // A mouse event for the open list's scrollbar: press, motion with the
    // button held, release (the dialog framework passes the last two while a
    // thumb is being dragged). True when it was the bar's.
    bool scrollMouse(int startx, int starty, int ev_x, int ev_y,
                     bool press, bool motion, bool release) {
        if (!dropdown_open || !scrollable()) { bar_.cancelDrag(); return false; }
        layoutBar(startx, starty);
        int top;
        if (!bar_.mouse(ev_x, ev_y, press, motion, release, top)) return false;
        scrollBy(top - dropdown_top);
        return true;
    }
    bool scrollDragging() const { return bar_.dragging(); }

    // Moves the open list's window by `rows` (negative: up) without changing
    // the selection; the highlight is kept inside the window, so Enter picks
    // something visible.
    void scrollBy(int rows) {
        if (!dropdown_open || items.empty()) return;
        const int visible = visibleRows();
        dropdown_top = std::clamp(dropdown_top + rows, 0, std::max(0, (int)items.size() - visible));
        dropdown_cursor = std::clamp(dropdown_cursor, dropdown_top, dropdown_top + visible - 1);
    }

    // A mouse wheel turn while the list is open: scrolls it, wherever the
    // pointer is (the open list is where the user is working). Returns true
    // when it was the list's.
    bool handleWheel(bool up) {
        if (!dropdown_open || items.empty()) return false;
        scrollBy(up ? -WHEEL_ROWS : WHEEL_ROWS);
        return true;
    }

    static constexpr int WHEEL_ROWS = 3;

    //  Keys
    // Returns true when the key was the combo's. While open, a key it has no
    // use for closes the list and is left for the dialog.
    bool handleKey(wint_t ch) {
        if (items.empty()) return false;
        const int last = (int)items.size() - 1;

        if (dropdown_open) {
            switch (ch) {
            case KEY_UP:    moveCursor(dropdown_cursor - 1);              return true;
            case KEY_DOWN:  moveCursor(dropdown_cursor + 1);              return true;
            case KEY_PPAGE: moveCursor(dropdown_cursor - visibleRows());  return true;
            case KEY_NPAGE: moveCursor(dropdown_cursor + visibleRows());  return true;
            case KEY_HOME:  moveCursor(0);                                return true;
            case KEY_END:   moveCursor(last);                             return true;
            case KEY_ENTER: case 10: case 13:
                close();
                select(dropdown_cursor);
                return true;
            case 27: case KEY_CTRL_SPACE: case KEY_CTRL_DOWN: case 9: case KEY_BTAB:
                close();
                return true;
            default:
                close();
                return false;
            }
        }

        switch (ch) {
        case KEY_UP:   select(selected_idx - 1); return true;   // at an end: nothing, but stay here
        case KEY_DOWN: select(selected_idx + 1); return true;
        case KEY_CTRL_SPACE:
        case KEY_CTRL_DOWN:
            open();
            return true;
        default:
            return false;
        }
    }

    //  Mouse
    // A left-button press at (ev_x, ev_y), dialog origin startx/starty.
    //   closed: on the arrow -> open; elsewhere on the field -> consumed (the
    //           owner focuses the combo); off the field -> not consumed
    //   open:   on an item -> select it and close; on the field -> close;
    //           anywhere else -> close, not consumed
    bool handleMouse(int startx, int starty, int ev_x, int ev_y) {
        if (items.empty()) return false;
        const bool on_field = ev_y == starty + y && ev_x >= startx + x && ev_x < startx + x + w;
        const bool on_arrow = on_field && ev_x >= startx + x + w - 4;

        if (dropdown_open) {
            int sx, sy;
            listOrigin(startx, starty, sx, sy);
            const int visible = visibleRows();
            // The scrollbar scrolls; it never closes the list.
            if (scrollable() && scrollMouse(startx, starty, ev_x, ev_y, true, false, false))
                return true;
            if (ev_x >= sx + 1 && ev_x < sx + w - 1 && ev_y >= sy + 1 && ev_y <= sy + visible) {
                const int idx = dropdown_top + (ev_y - (sy + 1));
                close();
                if (idx < (int)items.size()) select(idx);
                return true;
            }
            close();
            return on_field;
        }
        if (on_arrow) { open(); return true; }
        return on_field;
    }

private:
    mutable ScrollBar bar_;     // the open list's scrollbar, on its frame's right edge

    // A scrollbar needs the ▲, the ▼ and at least one track cell.
    bool scrollable() const {
        return (int)items.size() > visibleRows() && visibleRows() >= 3;
    }
    void layoutBar(int startx, int starty) const {
        int sx, sy;
        listOrigin(startx, starty, sx, sy);
        bar_.layout(sx + w - 1, sy + 1, visibleRows(), true,
                    (int)items.size(), visibleRows(), dropdown_top);
    }
    void listOrigin(int startx, int starty, int& sx, int& sy) const {
        sx = startx + x;
        sy = open_up ? starty + y - (visibleRows() + 2) : starty + y + 1;
    }
    void keepCursorVisible() {
        const int visible = visibleRows();
        if (dropdown_cursor < dropdown_top) dropdown_top = dropdown_cursor;
        if (dropdown_cursor >= dropdown_top + visible) dropdown_top = dropdown_cursor - visible + 1;
        dropdown_top = std::clamp(dropdown_top, 0, std::max(0, (int)items.size() - visible));
    }
    void moveCursor(int idx) {
        dropdown_cursor = std::clamp(idx, 0, (int)items.size() - 1);
        keepCursorVisible();
    }
};

//  TabControl 
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

    // Hit-test a click against the tab labels. startx/starty are the dialog's
    // top-left on screen (same origin draw() is given). Returns the clicked tab
    // index, or -1 if the point is not on a label. Geometry mirrors draw():
    // each label is " name " and labels are separated by a single space.
    int hitTest(int startx, int starty, int ev_x, int ev_y) const {
        if (ev_y != starty + y) return -1;
        int cur_x = startx + x;
        for (int i = 0; i < (int)tab_names.size(); ++i) {
            int len = (int)tab_names[i].size() + 2;   // leading + trailing space
            if (ev_x >= cur_x && ev_x < cur_x + len) return i;
            cur_x += len + 1;                          // +1 gap between labels
        }
        return -1;
    }

    int activeTab() const noexcept { return active_tab; }
};

//  OptionList 
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
        int*  ic_val     = nullptr;   // checkbox kept as an int (0/1), instead of b_val
        bool  disabled   = false;     // shown, marked as such, cannot be changed
    };

    std::vector<Option> options;  // set by the subclass each time the tab changes
    int cursor    = 0;            // currently highlighted option index
    int top_row   = 0;            // scroll offset (option index of first visible row)
    int x, y;                     // position relative to dialog top-left
    int visible_rows;
    int width = 0;                // columns available; 0 = unlimited
    // A list longer than its rows gets a scrollbar in the column just right
    // of them (needs a width).
    mutable ScrollBar bar;

    //  Flat row view (group headers interspersed) 
    struct Row { bool is_group; std::string text; int opt_idx; };

    std::vector<Row> buildRows() const {
        std::vector<Row> rows;
        std::string last_group;
        for (int i = 0; i < (int)options.size(); ++i) {
            if (options[i].group != last_group) {
                last_group = options[i].group;
                rows.push_back({ true, "- " + last_group, -1 });
            }
            bool val = options[i].is_radio ? (*options[i].i_val == options[i].radio_val)
                     : options[i].ic_val  ? (*options[i].ic_val != 0)
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
                const bool off = options[row.opt_idx].disabled;
                // A long label is shortened, the "not supported" mark kept.
                const std::string mark = off ? "  - not supported" : "";
                std::string text = row.text;
                if (width > 0) {
                    const int avail = width - 2 - (int)mark.size();
                    if ((int)text.size() > avail && avail > 3) text = text.substr(0, avail - 3) + "...";
                }
                text += mark;
                renderer.drawText(startx + x + 2, starty + y + i, text,
                                  sel ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG,
                                  off ? A_DIM : A_NORMAL);
            }
        }
        layoutBar(startx, starty, (int)rows.size());
        if (hasBar((int)rows.size())) bar.draw(renderer, Renderer::CP_DIALOG);
    }

    //  Scrolling
    // Shows rows from `top` on. The highlighted option stays in view: when it
    // would scroll out, the nearest option still shown takes the highlight.
    void scrollTo(int top) {
        const auto rows = buildRows();
        const int n = (int)rows.size();
        top_row = std::clamp(top, 0, std::max(0, n - visible_rows));
        int sel_row = -1;
        for (int i = 0; i < n; ++i)
            if (!rows[i].is_group && rows[i].opt_idx == cursor) { sel_row = i; break; }
        const int last = std::min(n, top_row + visible_rows) - 1;
        if (sel_row >= top_row && sel_row <= last) return;
        if (sel_row < top_row) {
            for (int i = top_row; i <= last; ++i)
                if (!rows[i].is_group) { cursor = rows[i].opt_idx; return; }
        } else {
            for (int i = last; i >= top_row; --i)
                if (!rows[i].is_group) { cursor = rows[i].opt_idx; return; }
        }
    }

    // The wheel scrolls the list three rows at a time.
    void scrollWheel(bool up) { scrollTo(top_row + (up ? -3 : 3)); }

    // A mouse event for the scrollbar (press; motion and release while its
    // thumb is dragged). True when it was the bar's.
    bool scrollMouse(int startx, int starty, int mx, int my,
                     bool press, bool motion, bool release) {
        const int n = (int)buildRows().size();
        if (!hasBar(n)) { bar.cancelDrag(); return false; }
        layoutBar(startx, starty, n);
        int top;
        if (!bar.mouse(mx, my, press, motion, release, top)) return false;
        scrollTo(top);
        return true;
    }
    bool scrollDragging() const { return bar.dragging(); }

    bool hasBar(int nrows) const { return width > 0 && visible_rows >= 3 && nrows > visible_rows; }
    void layoutBar(int startx, int starty, int nrows) const {
        bar.layout(startx + x + width, starty + y, visible_rows, true, nrows, visible_rows, top_row);
    }

    bool handleKey(wint_t ch) {
        if (ch == KEY_UP && cursor > 0)
        { --cursor; return true; }
        if (ch == KEY_DOWN && cursor < (int)options.size() - 1)
        { ++cursor; return true; }
        if ((ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13)
            && cursor < (int)options.size()) {
            auto& opt = options[cursor];
            if (opt.disabled)    return true;
            if (opt.is_radio)    *opt.i_val  = opt.radio_val;
            else if (opt.ic_val) *opt.ic_val = *opt.ic_val ? 0 : 1;
            else                 *opt.b_val  = !*opt.b_val;
            return true;
        }
        return false;
    }

    // Reset cursor and scroll when the tab changes
    void reset() { cursor = 0; top_row = 0; bar.cancelDrag(); }
};

//  FocusGroup 
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

    // Set true when the subclass draws all widgets itself in onDraw.
    // FocusGroup::draw() will still draw the box/title but skip all widgets.
    bool draw_widgets_manually = false;

    int inner_focus = 0;

    // Total focusable items: cb + sp + combo + tab + optionlist
    // (RadioList is one unit; text_buffer has no separate focus - it's the whole group)
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

        // text_buffer or manually-drawn group: box is drawn above; widgets skipped
        if (text_buffer || draw_widgets_manually) return;

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

        // NOTE: an open combo dropdown is NOT drawn here. DialogBase draws the
        // single globally-open dropdown (ComboBox::s_open) last, after every
        // group and box, so nothing can paint over it.
    }

    bool handleKey(wint_t ch) {
        // Text buffer group: typing/backspace handled by DialogBase directly
        if (text_buffer) return false;

        // If any combo has its dropdown open, route everything there.
        // Tab/Shift-Tab closes the dropdown (consuming the key) instead of moving focus.
        for (auto& combo : comboboxes) {
            if (combo.dropdown_open) {
                if (ch == 9 || ch == KEY_BTAB) { combo.close(); return true; }
                return combo.handleKey(ch);
            }
        }

        int count = innerCount();
        // Up/Down move inner focus only when there are multiple focusable items
        // and no OptionList (OptionList absorbs Up/Down internally) - and not
        // off a combo box, whose items they step through.
        bool has_optionlist = !optionlists.empty();
        const int first_combo = (int)(checkboxes.size() + spinners.size());
        const bool on_combo = inner_focus >= first_combo &&
                              inner_focus <  first_combo + (int)comboboxes.size();
        if (!has_optionlist && count > 1 && !on_combo) {
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
