#include "DialogBase.h"
#include <ncurses.h>

// ── UTF-8 append helper (shared by dispatchChar and text_buffer typing) ─────────

static void appendUtf8(std::string& buf, wint_t ch)
{
    if      (ch < 0x80)    { buf += static_cast<char>(ch); }
    else if (ch < 0x800)   { buf += static_cast<char>(0xC0 |  (ch >> 6));
                              buf += static_cast<char>(0x80 |  (ch & 0x3F)); }
    else if (ch < 0x10000) { buf += static_cast<char>(0xE0 |  (ch >> 12));
                              buf += static_cast<char>(0x80 | ((ch >>  6) & 0x3F));
                              buf += static_cast<char>(0x80 |  (ch        & 0x3F)); }
    else                   { buf += static_cast<char>(0xF0 |  (ch >> 18));
                              buf += static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
                              buf += static_cast<char>(0x80 | ((ch >>  6) & 0x3F));
                              buf += static_cast<char>(0x80 |  (ch        & 0x3F)); }
}


// ═══════════════════════════════════════════════════════════════════════════════
// DialogBase.cpp
// ═══════════════════════════════════════════════════════════════════════════════

// ── Entry point ───────────────────────────────────────────────────────────────

DialogResult DialogBase::run(Renderer& renderer)
{
    renderer.hideCursor();
    const int starty = (renderer.getHeight() - h_) / 2;
    const int startx = (renderer.getWidth()  - w_) / 2;

    WINDOW* behind = newwin(h_ + 1, w_ + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h_, w_, FALSE);

    onInit();

    renderer.drawShadow(startx, starty, w_, h_);
    renderer.drawBoxWithTitle(startx, starty, w_, h_,
        Renderer::CP_DIALOG, Renderer::DOUBLE,
        " " + title_ + " ", Renderer::CP_DIALOG_TITLE, A_BOLD);

    nodelay(stdscr, FALSE);
    pressed_        = false;
    pending_button_ = nullptr;

    while (true) {
        drawFrame(renderer, startx, starty, pressed_);

        if (pressed_) {
            runPressAnimation(renderer, startx, starty);
            pressed_ = false;
            if (pending_button_) {
                HandleResult hr = pending_button_->on_activate();
                pending_button_ = nullptr;
                nodelay(stdscr, FALSE); // restore blocking mode in case sub-dialog changed it
                if (hr == HandleResult::CLOSE) break;
            } else {
                break;
            }
            continue;
        }

        wint_t ch = renderer.getChar();
        HandleResult hr = HandleResult::CONTINUE;

        if (ch == 27) {
            timeout(50);
            wint_t next = renderer.getChar();
            timeout(-1);
            if (next == ERR) { result_.cancel(); break; }
            hr = groups_.empty() ? dispatchAltKey(next)
                                 : dispatchGroupAltKey(next);
        } else {
            hr = groups_.empty() ? dispatchKey(ch)
                                 : dispatchGroupKey(ch);
        }

        if (hr == HandleResult::CLOSE) break;
    }

    copywin(behind, stdscr, 0, 0, starty, startx, starty + h_, startx + w_, FALSE);
    delwin(behind);
    nodelay(stdscr, TRUE);
    renderer.showCursor();
    return result_;
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void DialogBase::drawFrame(Renderer& renderer, int sx, int sy, bool pressed)
{
    clearInterior(sx, sy);
    onDraw(renderer, sx, sy);
    if (groups_.empty()) {
        drawInputs(renderer, sx, sy);
        // Button row: focused when focus_ == btn_row_focus_index_
        bool row_focused = (focus_ == btn_row_focus_index_);
        button_row_.draw(renderer, sx, sy, row_focused, pressed);
    } else {
        drawGroups(renderer, sx, sy, pressed);
    }
    placeCursor(renderer, sx, sy);
    renderer.refresh();
}

void DialogBase::clearInterior(int sx, int sy)
{
    wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
    for (int i = 1; i < h_ - 1; ++i)
        mvwaddstr(stdscr, sy + i, sx + 1, std::string(w_ - 2, ' ').c_str());
    wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
}

void DialogBase::drawInputs(Renderer& renderer, int sx, int sy)
{
    for (const auto& inp : inputs_) {
        if (!inp.label.empty())
            renderer.drawText(sx + inp.label_x, sy + inp.label_y,
                              inp.label, Renderer::CP_DIALOG);
        renderer.drawText(sx + inp.field_x, sy + inp.field_y,
                          std::string(inp.field_w, ' '), Renderer::CP_LIST_BOX);
        renderer.drawText(sx + inp.field_x, sy + inp.field_y,
                          inp.buffer, Renderer::CP_LIST_BOX);
    }
}

void DialogBase::drawGroups(Renderer& renderer, int sx, int sy, bool pressed)
{
    for (int g = 0; g < (int)groups_.size(); ++g)
        groups_[g].draw(renderer, sx, sy, g == group_focus_);

    bool row_focused = inGroupButtonRow();
    button_row_.draw(renderer, sx, sy, row_focused, pressed);
}

void DialogBase::placeCursor(Renderer& renderer, int sx, int sy)
{
    // Mode B: show cursor only when the focused group has a text_buffer
    if (!groups_.empty()) {
        if (onPlaceCursor(renderer, sx, sy)) return;   // subclass handled it
        if (!inGroupButtonRow() && group_focus_ < (int)groups_.size()) {
            const auto& g = groups_[group_focus_];
            if (g.text_buffer) {
                renderer.showCursor();
                // Position: the subclass sets box_x+2 as the field origin
                move(sy + g.box_y + 1,
                     sx + g.box_x + 2 + static_cast<int>(g.text_buffer->size()));
                return;
            }
        }
        renderer.hideCursor();
        return;
    }

    // Mode A
    for (const auto& inp : inputs_) {
        if (focus_ == inp.focus_index) {
            renderer.showCursor();
            move(sy + inp.field_y,
                 sx + inp.field_x + static_cast<int>(inp.buffer.size()));
            return;
        }
    }
    renderer.hideCursor();
}

void DialogBase::runPressAnimation(Renderer& renderer, int sx, int sy)
{
    napms(120);
    drawFrame(renderer, sx, sy, false);
    napms(80);
}

// ── Shared hotkey helper ──────────────────────────────────────────────────────

bool DialogBase::tryHotkeyActivate(char lower)
{
    for (int i = 0; i < (int)button_row_.buttons.size(); ++i) {
        Button& btn = button_row_.buttons[i];
        if (btn.hotkey() == lower) {
            // Move visual focus to the button row so the press animation is visible.
            button_row_.inner_focus = i;
            if (!groups_.empty())
                group_focus_ = static_cast<int>(groups_.size());
            else
                focus_ = btn_row_focus_index_;
            armButton(&btn);
            return true;
        }
    }
    return false;
}

// ══════════════════════════════════════════════════════════════════════════════
// Mode A dispatch
// ══════════════════════════════════════════════════════════════════════════════

HandleResult DialogBase::dispatchKey(wint_t ch)
{
    switch (ch) {
    case 9:
        if (focus_ == btn_row_focus_index_) {
            // On the button row: Tab moves to the next button.
            // Only when we pass the last button do we wrap back to focus 0.
            if (button_row_.inner_focus < (int)button_row_.buttons.size() - 1) {
                ++button_row_.inner_focus;          // stay on row, next button
            } else {
                button_row_.inner_focus = 0;        // reset row to first button
                focus_ = 0;                         // jump to first focus stop
            }
        } else {
            focus_ = (focus_ + 1) % focus_count_;
            // When Tab lands on the button row, always start at the first button
            if (focus_ == btn_row_focus_index_)
                button_row_.inner_focus = 0;
        }
        return HandleResult::CONTINUE;
    case KEY_BTAB:
        if (focus_ == btn_row_focus_index_) {
            // On the button row: Shift-Tab moves to the previous button.
            // Only when we pass the first button do we leave the row.
            if (button_row_.inner_focus > 0) {
                --button_row_.inner_focus;          // stay on row, prev button
            } else {
                // Jump to the last focus stop before the button row
                focus_ = (btn_row_focus_index_ - 1 + focus_count_) % focus_count_;
                // Leave inner_focus on the last button for consistency
                button_row_.inner_focus = (int)button_row_.buttons.size() - 1;
            }
        } else {
            focus_ = (focus_ + focus_count_ - 1) % focus_count_;
            // When Shift-Tab lands on the button row, start at the last button
            if (focus_ == btn_row_focus_index_)
                button_row_.inner_focus = (int)button_row_.buttons.size() - 1;
        }
        return HandleResult::CONTINUE;

    case KEY_UP:    return dispatchArrow(KEY_UP);
    case KEY_DOWN:  return dispatchArrow(KEY_DOWN);

    // Left/Right: if focus is on the button row, move within it; else nav graph
    case KEY_LEFT:
        if (focus_ == btn_row_focus_index_) {
            button_row_.handleNavKey(KEY_LEFT);
            return HandleResult::CONTINUE;
        }
        return dispatchArrow(KEY_LEFT);
    case KEY_RIGHT:
        if (focus_ == btn_row_focus_index_) {
            button_row_.handleNavKey(KEY_RIGHT);
            return HandleResult::CONTINUE;
        }
        return dispatchArrow(KEY_RIGHT);

    case KEY_BACKSPACE: case 127: case 8:
        return dispatchBackspace();

    case KEY_ENTER: case 10: case 13:
        return dispatchEnter();

    default:
        if (ch > 31 && ch < KEY_MIN) return dispatchChar(ch);
        return onKey(ch);
    }
}

HandleResult DialogBase::dispatchAltKey(wint_t ch)
{
    char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    tryHotkeyActivate(lower);
    return HandleResult::CONTINUE;
}

HandleResult DialogBase::dispatchArrow(wint_t ch)
{
    if (!nav_) return HandleResult::CONTINUE;
    Direction dir;
    switch (ch) {
    case KEY_UP:    dir = Direction::UP;    break;
    case KEY_DOWN:  dir = Direction::DOWN;  break;
    case KEY_LEFT:  dir = Direction::LEFT;  break;
    case KEY_RIGHT: dir = Direction::RIGHT; break;
    default:        return HandleResult::CONTINUE;
    }
    focus_ = nav_(focus_, dir);
    return HandleResult::CONTINUE;
}

HandleResult DialogBase::dispatchEnter()
{
    // If focus is on the button row, activate the currently focused button
    if (focus_ == btn_row_focus_index_) {
        Button* btn = button_row_.focusedButton();
        if (btn) armButton(btn);
        return HandleResult::CONTINUE;
    }
    return HandleResult::CONTINUE;
}

HandleResult DialogBase::dispatchBackspace()
{
    for (auto& inp : inputs_) {
        if (focus_ != inp.focus_index || inp.buffer.empty()) continue;
        while (!inp.buffer.empty() &&
               (static_cast<unsigned char>(inp.buffer.back()) & 0xC0) == 0x80)
            inp.buffer.pop_back();
        if (!inp.buffer.empty()) inp.buffer.pop_back();
        return HandleResult::CONTINUE;
    }
    return HandleResult::CONTINUE;
}

HandleResult DialogBase::dispatchChar(wint_t ch)
{
    for (auto& inp : inputs_) {
        if (focus_ != inp.focus_index) continue;
        if (inp.numeric_only) {
            if (ch >= L'0' && ch <= L'9') inp.buffer += static_cast<char>(ch);
            return HandleResult::CONTINUE;
        }
        appendUtf8(inp.buffer, ch);
        return HandleResult::CONTINUE;
    }
    // No input field consumed this character — give the subclass a chance.
    return onKey(ch);
}

// ══════════════════════════════════════════════════════════════════════════════
// Mode B dispatch
// ══════════════════════════════════════════════════════════════════════════════

HandleResult DialogBase::dispatchGroupKey(wint_t ch)
{
    const int total_tabs = static_cast<int>(groups_.size())
                         + (button_row_.buttons.empty() ? 0 : 1);

    switch (ch) {
    case 9:
        if (!onTab(true)) {
            if (inGroupButtonRow()) {
                if (button_row_.inner_focus < (int)button_row_.buttons.size() - 1)
                    ++button_row_.inner_focus;
                else { button_row_.inner_focus = 0; group_focus_ = 0; }
            } else {
                group_focus_ = (group_focus_ + 1) % total_tabs;
                if (inGroupButtonRow()) button_row_.inner_focus = 0;
            }
        }
        return HandleResult::CONTINUE;
    case KEY_BTAB:
        if (!onTab(false)) {
            if (inGroupButtonRow()) {
                if (button_row_.inner_focus > 0) {
                    --button_row_.inner_focus;
                } else {
                    group_focus_ = total_tabs - 2;
                    if (group_focus_ < 0) group_focus_ = 0;
                    button_row_.inner_focus = (int)button_row_.buttons.size() - 1;
                }
            } else {
                group_focus_ = (group_focus_ + total_tabs - 1) % total_tabs;
                if (inGroupButtonRow())
                    button_row_.inner_focus = (int)button_row_.buttons.size() - 1;
            }
        }
        return HandleResult::CONTINUE;

    case KEY_LEFT:
        if (inGroupButtonRow()) {
            button_row_.handleNavKey(KEY_LEFT);
            return HandleResult::CONTINUE;
        }
        break;
    case KEY_RIGHT:
        if (inGroupButtonRow()) {
            button_row_.handleNavKey(KEY_RIGHT);
            return HandleResult::CONTINUE;
        }
        break;

    case KEY_ENTER: case 10: case 13:
        if (inGroupButtonRow()) {
            Button* btn = button_row_.focusedButton();
            if (btn) armButton(btn);
            return HandleResult::CONTINUE;
        }
        break;
    }

    // Delegate to the focused group
    if (!inGroupButtonRow() && group_focus_ < (int)groups_.size()) {
        auto& g = groups_[group_focus_];

        // text_buffer group: backspace and printable chars go directly to the buffer
        if (g.text_buffer) {
            if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                if (!g.text_buffer->empty()) {
                    // UTF-8 aware pop
                    auto& buf = *g.text_buffer;
                    while (!buf.empty() &&
                           (static_cast<unsigned char>(buf.back()) & 0xC0) == 0x80)
                        buf.pop_back();
                    if (!buf.empty()) buf.pop_back();
                }
                return HandleResult::CONTINUE;
            }
            if (ch > 31 && ch < KEY_MIN) {
                appendUtf8(*g.text_buffer, ch);
                return HandleResult::CONTINUE;
            }
            return onKey(ch);   // let subclass handle arrows, etc.
        }

        // Remember the active tab before the key so we can detect tab changes
        int old_tab = -1;
        for (auto& tc : g.tabcontrols) old_tab = tc.active_tab;

        // If the group has no widgets at all (e.g. a read-only display group),
        // fall through to onKey so the subclass can handle the key (e.g. scroll).
        bool group_has_widgets = !g.checkboxes.empty()  || !g.spinners.empty()   ||
                                 !g.comboboxes.empty()  || !g.tabcontrols.empty()||
                                 !g.optionlists.empty() || !g.radiolists.empty();

        if (group_has_widgets) {
            bool consumed = g.handleKey(ch);

            // If a TabControl changed its active tab, reset all OptionLists
            for (auto& tc : g.tabcontrols) {
                if (tc.active_tab != old_tab) {
                    for (auto& ol : g.optionlists) ol.reset();
                    break;
                }
            }

            if (consumed) return HandleResult::CONTINUE;
        }

        // Key was not consumed by any widget — give the subclass a chance.
        return onKey(ch);
    }

    return HandleResult::CONTINUE;
}

HandleResult DialogBase::dispatchGroupAltKey(wint_t ch)
{
    char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    // Group hotkeys jump focus to the group
    for (int g = 0; g < (int)groups_.size(); ++g) {
        if (groups_[g].hotkey == lower) {
            group_focus_ = g;
            return HandleResult::CONTINUE;
        }
    }

    // Button hotkeys trigger the press animation
    tryHotkeyActivate(lower);
    return HandleResult::CONTINUE;
}
