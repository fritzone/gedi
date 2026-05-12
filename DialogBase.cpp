#include "DialogBase.h"
#include <ncurses.h>

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

    copywin(behind, stdscr, 0, 0, starty, startx, h_, w_, FALSE);
    delwin(behind);
    nodelay(stdscr, TRUE);
    renderer.showCursor();
    return result_;
}

// ── Frame rendering ───────────────────────────────────────────────────────────

void DialogBase::drawFrame(Renderer& renderer, int sx, int sy, bool pressed)
{
    clearInterior(sx, sy);
    onDraw(renderer, sx, sy);
    if (groups_.empty()) {
        drawInputs (renderer, sx, sy);
        drawButtons(renderer, sx, sy, pressed);
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

void DialogBase::drawButtons(Renderer& renderer, int sx, int sy, bool pressed)
{
    for (const auto& btn : buttons_) {
        bool sel  = (focus_ == btn.focus_index);
        bool pres = pressed && sel;
        renderer.drawButton(sx + btn.btn_x, sy + btn.btn_y, btn.label, sel, pres);
    }
}

void DialogBase::drawGroups(Renderer& renderer, int sx, int sy, bool pressed)
{
    for (int g = 0; g < (int)groups_.size(); ++g)
        groups_[g].draw(renderer, sx, sy, g == group_focus_);

    // Button row — last tab stop
    bool btn_row_focused = inGroupButtonRow();
    for (const auto& btn : group_buttons_) {
        bool sel  = btn_row_focused && (btn.focus_index == group_btn_focus_);
        bool pres = pressed && sel;
        renderer.drawButton(sx + btn.btn_x, sy + btn.btn_y, btn.label, sel, pres);
    }
}

void DialogBase::placeCursor(Renderer& renderer, int /*sx*/, int /*sy*/)
{
    // Groups and buttons never show a text cursor.
    if (!groups_.empty()) { renderer.hideCursor(); return; }

    for (const auto& inp : inputs_) {
        if (focus_ == inp.focus_index) {
            renderer.showCursor();
            // starty/startx are baked into field_y/field_x as absolute offsets
            // from the dialog corner, but we need screen coords here.
            // The caller passes sx/sy; we stored them via the frame helpers.
            // For simplicity we re-derive them from the stored w_/h_ — but
            // actually we don't have them here. Accept that cursor placement
            // for Mode A is handled by the subclass via onDraw if needed,
            // OR we store sx/sy as members when drawFrame is called.
            // For now hide — Mode A subclasses override placeCursor via onDraw.
            renderer.hideCursor();
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

// ══════════════════════════════════════════════════════════════════════════════
// Mode A dispatch
// ══════════════════════════════════════════════════════════════════════════════

HandleResult DialogBase::dispatchKey(wint_t ch)
{
    switch (ch) {
    case 9:      focus_ = (focus_ + 1)              % focus_count_; return HandleResult::CONTINUE;
    case KEY_BTAB: focus_ = (focus_ + focus_count_ - 1) % focus_count_; return HandleResult::CONTINUE;
    case KEY_UP:    return dispatchArrow(KEY_UP);
    case KEY_DOWN:  return dispatchArrow(KEY_DOWN);
    case KEY_LEFT:  return dispatchArrow(KEY_LEFT);
    case KEY_RIGHT: return dispatchArrow(KEY_RIGHT);
    case KEY_BACKSPACE: case 127: case 8: return dispatchBackspace();
    case KEY_ENTER: case 10: case 13:    return dispatchEnter();
    default:
        if (ch > 31 && ch < KEY_MIN) return dispatchChar(ch);
        return onKey(ch);
    }
}

HandleResult DialogBase::dispatchAltKey(wint_t ch)
{
    char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (auto& btn : buttons_) {
        if (btn.hotkey() == lower) {
            focus_          = btn.focus_index;
            pending_button_ = &btn;
            pressed_        = true;
            return HandleResult::CONTINUE;
        }
    }
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
    for (auto& btn : buttons_) {
        if (focus_ == btn.focus_index) {
            pending_button_ = &btn;
            pressed_        = true;
            return HandleResult::CONTINUE;
        }
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
        if      (ch < 0x80)    { inp.buffer += static_cast<char>(ch); }
        else if (ch < 0x800)   { inp.buffer += static_cast<char>(0xC0|(ch>>6));
                                  inp.buffer += static_cast<char>(0x80|(ch&0x3F)); }
        else if (ch < 0x10000) { inp.buffer += static_cast<char>(0xE0|(ch>>12));
                                  inp.buffer += static_cast<char>(0x80|((ch>>6)&0x3F));
                                  inp.buffer += static_cast<char>(0x80|(ch&0x3F)); }
        else                   { inp.buffer += static_cast<char>(0xF0|(ch>>18));
                                  inp.buffer += static_cast<char>(0x80|((ch>>12)&0x3F));
                                  inp.buffer += static_cast<char>(0x80|((ch>>6)&0x3F));
                                  inp.buffer += static_cast<char>(0x80|(ch&0x3F)); }
        return HandleResult::CONTINUE;
    }
    return HandleResult::CONTINUE;
}

// ══════════════════════════════════════════════════════════════════════════════
// Mode B dispatch (focus groups)
// ══════════════════════════════════════════════════════════════════════════════

HandleResult DialogBase::dispatchGroupKey(wint_t ch)
{
    const int total_tabs = static_cast<int>(groups_.size())
                         + (group_buttons_.empty() ? 0 : 1);

    switch (ch) {
    // ── Tab / Shift-Tab: cycle between groups and the button row ──────────────
    case 9:
        group_focus_ = (group_focus_ + 1) % total_tabs;
        return HandleResult::CONTINUE;
    case KEY_BTAB:
        group_focus_ = (group_focus_ + total_tabs - 1) % total_tabs;
        return HandleResult::CONTINUE;

    // ── Button row: Left/Right move between buttons, Enter activates ──────────
    case KEY_LEFT:
        if (inGroupButtonRow() && group_btn_focus_ > 0) {
            --group_btn_focus_;
            return HandleResult::CONTINUE;
        }
        break;
    case KEY_RIGHT:
        if (inGroupButtonRow() &&
            group_btn_focus_ < (int)group_buttons_.size() - 1) {
            ++group_btn_focus_;
            return HandleResult::CONTINUE;
        }
        break;
    case KEY_ENTER: case 10: case 13:
        if (inGroupButtonRow()) {
            for (auto& btn : group_buttons_) {
                if (btn.focus_index == group_btn_focus_) {
                    pending_button_ = &btn;
                    pressed_        = true;
                    return HandleResult::CONTINUE;
                }
            }
        }
        break;
    }

    // ── Delegate everything else to the focused group ─────────────────────────
    if (!inGroupButtonRow() && group_focus_ < (int)groups_.size()) {
        groups_[group_focus_].handleKey(ch);
    }
    return HandleResult::CONTINUE;
}

HandleResult DialogBase::dispatchGroupAltKey(wint_t ch)
{
    char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    // Check group hotkeys first
    for (int g = 0; g < (int)groups_.size(); ++g) {
        if (groups_[g].hotkey == lower) {
            group_focus_ = g;
            return HandleResult::CONTINUE;
        }
    }

    // Then button hotkeys — these trigger the press animation
    for (auto& btn : group_buttons_) {
        if (btn.hotkey() == lower) {
            group_focus_     = static_cast<int>(groups_.size()); // button row
            group_btn_focus_ = btn.focus_index;
            pending_button_  = &btn;
            pressed_         = true;
            return HandleResult::CONTINUE;
        }
    }
    return HandleResult::CONTINUE;
}
