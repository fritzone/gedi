#include "ReplaceDialog.h"
#include "utils.h"
#include "CyclicEnum.h"

#include <ncurses.h>
#include <type_traits>

ReplaceResult ReplaceDialog::show(Renderer& renderer,
                                                  const std::string& initial_find,
                                                  const std::string& initial_replace)
{
    renderer.hideCursor();

    int h = 10, w = 55;
    int starty = (renderer.getHeight() - h) / 2;
    int startx = (renderer.getWidth()  - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

    std::string find_buf    = initial_find;
    std::string replace_buf = initial_replace;

    // Focus order: FIND → REPLACE → BTN_REPLACE → BTN_REPLACE_ALL → BTN_CANCEL
    DeclareCyclicEnum(Focus,
                      FIND,
                      REPLACE,
                      BTN_REPLACE,
                      BTN_REPLACE_ALL,
                      BTN_CANCEL
                      ) focus = Focus::FIND;

    std::string replace_btn_text     = " &Replace ";
    std::string replace_all_btn_text = " Replace &All ";
    std::string cancel_btn_text      = " &Cancel ";

    renderer.drawShadow(startx, starty, w, h);
    renderer.drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG,
                              Renderer::DOUBLE, " Replace ", Renderer::CP_DIALOG_TITLE, A_BOLD);

    ReplaceResult result = { ReplaceAction::CANCEL, initial_find, initial_replace };
    bool pressed = false;
    nodelay(stdscr, FALSE);

    while (true) {
        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        for (int i = 1; i < h - 1; ++i)
            mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        renderer.drawText(startx + 3,  starty + 2, "Find what:",    Renderer::CP_DIALOG);
        renderer.drawText(startx + 3,  starty + 4, "Replace with:", Renderer::CP_DIALOG);

        renderer.drawText(startx + 17, starty + 2, std::string(w - 20, ' '), Renderer::CP_LIST_BOX);
        renderer.drawText(startx + 17, starty + 4, std::string(w - 20, ' '), Renderer::CP_LIST_BOX);
        renderer.drawText(startx + 18, starty + 2, find_buf,    Renderer::CP_LIST_BOX);
        renderer.drawText(startx + 18, starty + 4, replace_buf, Renderer::CP_LIST_BOX);

        int btn_y = starty + h - 3;
        renderer.drawButton(startx + 4,        btn_y, replace_btn_text,     focus == Focus::BTN_REPLACE,     pressed && focus == Focus::BTN_REPLACE);
        renderer.drawButton(startx + 18,       btn_y, replace_all_btn_text, focus == Focus::BTN_REPLACE_ALL, pressed && focus == Focus::BTN_REPLACE_ALL);
        renderer.drawButton(startx + w - 14,   btn_y, cancel_btn_text,      focus == Focus::BTN_CANCEL,      pressed && focus == Focus::BTN_CANCEL);

        if      (focus == Focus::FIND)    { renderer.showCursor(); move(starty + 2, startx + 18 + find_buf.length()); }
        else if (focus == Focus::REPLACE) { renderer.showCursor(); move(starty + 4, startx + 18 + replace_buf.length()); }
        else                              { renderer.hideCursor(); }

        renderer.refresh();

        if (pressed) { napms(100); goto end_dialog; }

        wint_t ch = renderer.getChar();

        if (ch == 27) { // ESC or Alt
            timeout(50);
            wint_t next_ch = renderer.getChar();
            timeout(-1);
            if (next_ch == ERR) {
                break;
            } else {
                if      (tolower(next_ch) == 'r') { focus = Focus::BTN_REPLACE;     result = { ReplaceAction::REPLACE,     find_buf, replace_buf }; pressed = true; }
                else if (tolower(next_ch) == 'a') { focus = Focus::BTN_REPLACE_ALL; result = { ReplaceAction::REPLACE_ALL, find_buf, replace_buf }; pressed = true; }
                else if (tolower(next_ch) == 'c') { focus = Focus::BTN_CANCEL;      pressed = true; }
            }
        } else {
            switch (ch) {
            case 9:        focus = cycle_next(focus); break;  // Tab
            case KEY_BTAB: focus = cycle_prev(focus); break;  // Shift-Tab

            case KEY_UP:
                if (focus == Focus::REPLACE) focus = Focus::FIND;
                break;
            case KEY_DOWN:
                if (focus == Focus::FIND)    focus = Focus::REPLACE;
                break;
            case KEY_LEFT:
                if (focus == Focus::BTN_REPLACE_ALL) focus = Focus::BTN_REPLACE;
                if (focus == Focus::BTN_CANCEL)      focus = Focus::BTN_REPLACE_ALL;
                break;
            case KEY_RIGHT:
                if (focus == Focus::BTN_REPLACE)     focus = Focus::BTN_REPLACE_ALL;
                if (focus == Focus::BTN_REPLACE_ALL) focus = Focus::BTN_CANCEL;
                break;

            case KEY_BACKSPACE: case 127: case 8:
                if (focus == Focus::FIND    && !find_buf.empty())    find_buf.pop_back();
                if (focus == Focus::REPLACE && !replace_buf.empty()) replace_buf.pop_back();
                break;

            case KEY_ENTER: case 10: case 13:
                if      (focus == Focus::BTN_REPLACE)     { result = { ReplaceAction::REPLACE,     find_buf, replace_buf }; pressed = true; }
                else if (focus == Focus::BTN_REPLACE_ALL) { result = { ReplaceAction::REPLACE_ALL, find_buf, replace_buf }; pressed = true; }
                else if (focus == Focus::BTN_CANCEL)      { pressed = true; }
                break;

            default:
                if (ch > 31 && ch < KEY_MIN) {
                    std::string utf8_char = wchar_to_utf8(ch);
                    if (focus == Focus::FIND)    find_buf    += utf8_char;
                    if (focus == Focus::REPLACE) replace_buf += utf8_char;
                }
                break;
            }
        }
    }

end_dialog:
    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE);
    delwin(behind);
    nodelay(stdscr, TRUE);
    renderer.showCursor();
    return result;
}
