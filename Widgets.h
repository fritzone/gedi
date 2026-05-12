#pragma once
#include <ncurses.h>
#include "Renderer.h"
#include <string>
#include <vector>
#include <functional>

// ═══════════════════════════════════════════════════════════════════════════════
// Widgets.h
//
// Self-contained interactive widget descriptors used by DialogBase.
// Each widget knows how to draw itself and handle its own key events.
//
// Widgets are grouped into FocusGroups. Tab/Shift-Tab cycle between groups;
// Up/Down/Left/Right/Space move within the focused group.
//
// Supported widget types:
//   CheckBox    — [X] / [ ] toggle
//   Spinner     — < N >  integer value with left/right bounds
//   RadioList   — scrollable (•) / ( ) single-selection list
//   ButtonRow   — one or more buttons on a single row (existing ButtonDescriptor)
// ═══════════════════════════════════════════════════════════════════════════════

// ── CheckBox ──────────────────────────────────────────────────────────────────
struct CheckBox {
    std::string label;
    bool&       value;        // reference into the dialog's own state
    int         x, y;        // position relative to dialog top-left

    void draw(Renderer& renderer, int startx, int starty, bool focused) const {
        renderer.drawText(startx + x, starty + y,
                          value ? "[X]" : "[ ]",
                          focused ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
        renderer.drawText(startx + x + 4, starty + y, label, Renderer::CP_DIALOG);
    }

    // Returns true if the widget consumed the key.
    bool handleKey(wint_t ch) {
        if (ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13) {
            value = !value;
            return true;
        }
        return false;
    }
};

// ── Spinner ───────────────────────────────────────────────────────────────────
struct Spinner {
    std::string label;
    int&        value;        // reference into the dialog's own state
    int         min_val;
    int         max_val;
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
    int&  selected_idx;   // reference into the dialog's own state (committed value)
    int&  cursor_idx;     // reference into the dialog's own state (highlighted row)
    int   x, y;          // top-left of the list area, relative to dialog top-left
    int   visible_rows;  // how many rows are visible (list_height)

    void draw(Renderer& renderer, int startx, int starty, bool group_focused) const {
        int top = scrollOffset();
        for (int i = 0; i < visible_rows; ++i) {
            int idx = top + i;
            if (idx >= (int)items.size()) break;
            bool item_focused = group_focused && (idx == cursor_idx);
            renderer.drawText(startx + x, starty + y + i,
                              idx == selected_idx ? "(•)" : "( )",
                              item_focused ? Renderer::CP_MENU_SELECTED
                                           : Renderer::CP_DIALOG);
            renderer.drawText(startx + x + 4, starty + y + i,
                              items[idx], Renderer::CP_DIALOG);
        }
    }

    bool handleKey(wint_t ch) {
        if (ch == KEY_UP && cursor_idx > 0) {
            --cursor_idx;
            return true;
        }
        if (ch == KEY_DOWN && cursor_idx < (int)items.size() - 1) {
            ++cursor_idx;
            return true;
        }
        if (ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13) {
            selected_idx = cursor_idx;
            return true;
        }
        return false;
    }

    int scrollOffset() const {
        if (cursor_idx >= visible_rows)
            return cursor_idx - visible_rows + 1;
        return 0;
    }
};

// ── FocusGroup ────────────────────────────────────────────────────────────────
// A named group of widgets inside a box. Tab cycles between groups;
// Up/Down/Left/Right/Space are dispatched to the active widget within a group.

struct FocusGroup {
    std::string  title;       // box title, e.g. " &Indentation "
    char         hotkey;      // Alt+hotkey jumps to this group ('\0' = none)
    int          box_x, box_y, box_w, box_h;  // the surrounding single-line box

    // Widgets inside this group — at most one type per group is typical,
    // but you may mix them. Up/Down moves between widgets within the group;
    // the active widget then handles Left/Right/Space.
    std::vector<CheckBox>  checkboxes;
    std::vector<Spinner>   spinners;
    std::vector<RadioList> radiolists;

    // Which item within this group has focus (index across all widgets in order)
    int inner_focus = 0;

    // Total number of inner focusable items
    int innerCount() const {
        return static_cast<int>(checkboxes.size() + spinners.size());
        // RadioList counts as one focusable unit (Up/Down moves within it)
    }

    void draw(Renderer& renderer, int startx, int starty, bool focused) const {
        int title_flags = focused ? A_BOLD : 0;
        renderer.drawBoxWithTitle(startx + box_x, starty + box_y,
                                  box_w, box_h,
                                  Renderer::CP_DIALOG, Renderer::SINGLE,
                                  title, Renderer::CP_DIALOG, title_flags);

        int item = 0;
        for (const auto& cb : checkboxes)
            cb.draw(renderer, startx, starty, focused && inner_focus == item++);
        for (const auto& sp : spinners)
            sp.draw(renderer, startx, starty, focused && inner_focus == item++);
        for (const auto& rl : radiolists)
            rl.draw(renderer, startx, starty, focused);
    }

    // Dispatch a key to the currently focused inner widget.
    // Returns true if consumed.
    bool handleKey(wint_t ch) {
        // Up/Down move inner focus (for checkboxes + spinners)
        int count = innerCount();
        if (count > 1) {
            if (ch == KEY_UP   && inner_focus > 0)       { --inner_focus; return true; }
            if (ch == KEY_DOWN && inner_focus < count-1) { ++inner_focus; return true; }
        }

        // Dispatch to the focused item
        int item = 0;
        for (auto& cb : checkboxes) {
            if (inner_focus == item++) return cb.handleKey(ch);
        }
        for (auto& sp : spinners) {
            if (inner_focus == item++) return sp.handleKey(ch);
        }
        for (auto& rl : radiolists) {
            return rl.handleKey(ch);
        }
        return false;
    }
};
