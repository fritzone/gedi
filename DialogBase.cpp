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

    if (!background_fn_) {
        renderer.drawShadow(startx, starty, w_, h_);
        renderer.drawBoxWithTitle(startx, starty, w_, h_,
            Renderer::CP_DIALOG, Renderer::DOUBLE,
            " " + title_ + " ", Renderer::CP_DIALOG_TITLE, A_BOLD);
    }

    nodelay(stdscr, FALSE);
    pressed_             = false;
    pending_button_      = nullptr;
    mouse_capture_btn_   = -1;
    mouse_hover_pressed_ = false;

    while (true) {
        if (background_fn_) {
            background_fn_();
            renderer.drawShadow(startx, starty, w_, h_);
            renderer.drawBoxWithTitle(startx, starty, w_, h_,
                Renderer::CP_DIALOG, Renderer::DOUBLE,
                " " + title_ + " ", Renderer::CP_DIALOG_TITLE, A_BOLD);
        }
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

        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK)
                hr = dispatchMouse(ev, startx, starty);
        } else if (ch == 27) {
            timeout(50);
            wint_t next = renderer.getChar();
            timeout(-1);
            if (next == (wint_t)ERR) { result_.cancel(); break; }

            if (next == '[') {
                // CSI sequence — read until the final byte (0x40–0x7E)
                std::string csi;
                timeout(30);
                wint_t c;
                while ((c = renderer.getChar()) != (wint_t)ERR && csi.size() < 16) {
                    csi += static_cast<char>(c);
                    if (c >= 64 && c <= 126) break;
                }
                timeout(-1);
                if (csi == "1;5B") {  // Ctrl+Down
                    hr = groups_.empty() ? dispatchKey(525)
                                         : dispatchGroupKey(525);
                }
                // other CSI sequences: ignore
            } else {
                hr = groups_.empty() ? dispatchAltKey(next)
                                     : dispatchGroupAltKey(next);
            }
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
    // Blend in the hover-pressed state so the captured button appears pressed
    // during mouse-hold, before the release actually fires the action.
    bool show_pressed = pressed || mouse_hover_pressed_;
    clearInterior(sx, sy);
    onDraw(renderer, sx, sy);
    if (groups_.empty()) {
        drawInputs(renderer, sx, sy);
        // Button row: focused when focus_ == btn_row_focus_index_
        bool row_focused = (focus_ == btn_row_focus_index_);
        button_row_.draw(renderer, sx, sy, row_focused, show_pressed);
    } else {
        drawGroups(renderer, sx, sy, show_pressed);
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

    case ' ':
        if (focus_ == btn_row_focus_index_) return dispatchEnter();
        return dispatchChar(' ');

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

    case KEY_ENTER: case 10: case 13: case ' ':
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

// ══════════════════════════════════════════════════════════════════════════════
// Mouse dispatch
// ══════════════════════════════════════════════════════════════════════════════

static int buttonVisualLen(const std::string& label) {
    int n = 0;
    for (char c : label) if (c != '&') ++n;
    return n;
}

HandleResult DialogBase::dispatchMouse(const MEVENT& ev, int startx, int starty)
{
    bool is_press     = (ev.bstate & BUTTON1_PRESSED)        != 0;
    bool is_release   = (ev.bstate & BUTTON1_RELEASED)       != 0;
    bool is_clicked   = (ev.bstate & BUTTON1_CLICKED)        != 0; // synthesised (rare with mouseinterval=0)
    bool is_scroll_up = (ev.bstate & BUTTON4_PRESSED)        != 0;
    bool is_scroll_dn = (ev.bstate & BUTTON5_PRESSED)        != 0;

    // Treat position-reports while a button is captured as drag/motion events.
    if (!is_press && !is_release && !is_clicked && !is_scroll_up && !is_scroll_dn) {
        if (mouse_capture_btn_ < 0) return HandleResult::CONTINUE;
    }

    // Helper: is the mouse currently over button[i]?
    auto overButton = [&](int i) -> bool {
        const auto& btn = button_row_.buttons[i];
        int bx = startx + btn.x;
        int by = starty + btn.y;
        int bw = buttonVisualLen(btn.label);
        return (ev.y == by && ev.x >= bx && ev.x < bx + bw);
    };

    // ── Synthesised BUTTON1_CLICKED (press + release in one event) ────────────
    // With mouseinterval(0) this is rare, but handle it for robustness:
    // treat as an immediate full click bypassing the capture machinery.
    if (is_clicked && !is_press && mouse_capture_btn_ < 0) {
        for (int i = 0; i < (int)button_row_.buttons.size(); ++i) {
            if (overButton(i)) {
                button_row_.inner_focus = i;
                if (!groups_.empty())
                    group_focus_ = static_cast<int>(groups_.size());
                else
                    focus_ = btn_row_focus_index_;
                armButton(&button_row_.buttons[i]);
                return HandleResult::CONTINUE;
            }
        }
        // Fall through to focus-change hit-tests below.
        is_press = true; // treat remainder as a press for focus changes
    }

    // ── Events while a button is captured ────────────────────────────────────
    if (mouse_capture_btn_ >= 0) {
        bool still_over = overButton(mouse_capture_btn_);

        if (is_release || is_clicked) {
            if (still_over) {
                // User released on the same button they pressed → fire it.
                armButton(&button_row_.buttons[mouse_capture_btn_]);
            }
            mouse_capture_btn_   = -1;
            mouse_hover_pressed_ = false;
            return HandleResult::CONTINUE;
        }

        // Drag/motion: update the visual pressed state, nothing else.
        mouse_hover_pressed_ = still_over;
        return HandleResult::CONTINUE;
    }

    // ── No active capture ─────────────────────────────────────────────────────
    if (!is_press && !is_scroll_up && !is_scroll_dn)
        return HandleResult::CONTINUE;

    // Button-row press: capture the button, show it as pressed.
    if (is_press) {
        for (int i = 0; i < (int)button_row_.buttons.size(); ++i) {
            if (overButton(i)) {
                mouse_capture_btn_   = i;
                mouse_hover_pressed_ = true;
                button_row_.inner_focus = i;
                if (!groups_.empty())
                    group_focus_ = static_cast<int>(groups_.size());
                else
                    focus_ = btn_row_focus_index_;
                return HandleResult::CONTINUE;
            }
        }
    }

    // Mode A: input field focus on click
    if (groups_.empty() && is_press) {
        for (int i = 0; i < (int)inputs_.size(); ++i) {
            const auto& inp = inputs_[i];
            int fx = startx + inp.field_x;
            int fy = starty + inp.field_y;
            if (ev.y == fy && ev.x >= fx && ev.x < fx + inp.field_w) {
                focus_ = inp.focus_index;
                return HandleResult::CONTINUE;
            }
        }
    }

    // Mode B: click on a group to focus it; click on checkbox/spinner to activate
    if (!groups_.empty() && is_press) {
        for (int g = 0; g < (int)groups_.size(); ++g) {
            const auto& grp = groups_[g];
            int gx1 = startx + grp.box_x;
            int gy1 = starty + grp.box_y;
            int gx2 = gx1 + grp.box_w - 1;
            int gy2 = gy1 + grp.box_h - 1;
            if (ev.x >= gx1 && ev.x <= gx2 && ev.y >= gy1 && ev.y <= gy2) {
                group_focus_ = g;
                // Hit-test checkboxes
                for (auto& cb : groups_[g].checkboxes) {
                    int cx = startx + cb.x, cy = starty + cb.y;
                    if (ev.y == cy && ev.x >= cx && ev.x < cx + 3 + 1 + (int)cb.label.size()) {
                        cb.value = !cb.value;
                        return HandleResult::CONTINUE;
                    }
                }
                // Hit-test spinners (< and > arrows)
                for (auto& sp : groups_[g].spinners) {
                    int sx2 = startx + sp.x, sy2 = starty + sp.y;
                    if (ev.y == sy2) {
                        if (ev.x == sx2     && sp.value > sp.min_val) --sp.value;
                        else if (ev.x == sx2 + 4 && sp.value < sp.max_val) ++sp.value;
                    }
                }
                // OptionList click
                if (!groups_[g].optionlists.empty()) {
                    auto& ol = groups_[g].optionlists[0];
                    if (ev.y >= starty + ol.y && ev.y < starty + ol.y + ol.visible_rows) {
                        auto rows = ol.buildRows();
                        int row_i   = ev.y - (starty + ol.y);
                        int abs_row = ol.top_row + row_i;
                        if (abs_row >= 0 && abs_row < (int)rows.size() && !rows[abs_row].is_group) {
                            ol.cursor = rows[abs_row].opt_idx;
                            auto& opt = ol.options[ol.cursor];
                            if (opt.is_radio) *opt.i_val = opt.radio_val;
                            else              *opt.b_val = !*opt.b_val;
                        }
                    }
                }
                return HandleResult::CONTINUE;
            }
        }
    }

    // Scroll wheel on OptionList (works regardless of group focus)
    if ((is_scroll_up || is_scroll_dn) && !groups_.empty()) {
        for (int g = 0; g < (int)groups_.size(); ++g) {
            if (!groups_[g].optionlists.empty()) {
                auto& ol = groups_[g].optionlists[0];
                if (is_scroll_up && ol.cursor > 0) --ol.cursor;
                if (is_scroll_dn && ol.cursor < (int)ol.options.size() - 1) ++ol.cursor;
                return HandleResult::CONTINUE;
            }
        }
    }

    return HandleResult::CONTINUE;
}
