#include "GoToLineDialog.h"
#include "utils.h"
#include <ncurses.h>
#include <string>

int GoToLineDialog::show(Renderer& renderer, int current_line, int max_lines) {
    renderer.hideCursor();

    int h = 10, w = 50;
    int starty = (renderer.getHeight() - h) / 2;
    int startx = (renderer.getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

    std::string line_buf = std::to_string(current_line);
    int focus = 0; // 0: input, 1: Go, 2: Cancel

    std::string go_btn_text = " &Go ";
    std::string cancel_btn_text = " &Cancel ";

    renderer.drawShadow(startx, starty, w, h);
    renderer.drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::DOUBLE, " Go to Line ", Renderer::CP_DIALOG_TITLE, A_BOLD);

    nodelay(stdscr, FALSE);
    int result_line = -1;
    bool pressed = false;

    while (true) {
        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        for (int i = 1; i < h - 1; ++i) {
            mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
        }
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        renderer.drawText(startx + 3, starty + 2, "Line Number (1-" + std::to_string(max_lines) + "):", Renderer::CP_DIALOG);
        renderer.drawText(startx + 3, starty + 4, std::string(w - 6, ' '), Renderer::CP_LIST_BOX);
        renderer.drawText(startx + 19, starty + 4, line_buf, Renderer::CP_LIST_BOX);

        int btn_y = starty + h - 3;
        renderer.drawButton(startx + w / 2 - 12, btn_y, go_btn_text, focus == 1, pressed && focus == 1);
        renderer.drawButton(startx + w / 2 + 2, btn_y, cancel_btn_text, focus == 2, pressed && focus == 2);

        if (focus == 0) {
            renderer.showCursor();
            move(starty + 4, startx + 19 + line_buf.length());
        } else {
            renderer.hideCursor();
        }
        renderer.refresh();

        if (pressed) {
            napms(100);
            goto end_dialog;
        }

        wint_t ch = renderer.getChar();

        if (ch == 27) { // ESC or Alt
            timeout(50);
            wint_t next_ch = renderer.getChar();
            timeout(-1);
            if (next_ch == ERR) { break; }
            else {
                if (tolower(next_ch) == 'g') { focus = 1; pressed = true; }
                if (tolower(next_ch) == 'c') { focus = 2; pressed = true; }
            }
        } else {
            switch (ch) {
            case 9: focus = (focus + 1) % 3; break;
            case KEY_BTAB: focus = (focus + 2) % 3; break;
            case KEY_UP: if (focus > 0) focus = 0; break;
            case KEY_DOWN: if (focus == 0) focus = 1; break;
            case KEY_LEFT: if (focus == 2) focus = 1; break;
            case KEY_RIGHT: if (focus == 1) focus = 2; break;
            case KEY_BACKSPACE: case 127: case 8:
                if (focus == 0 && !line_buf.empty()) line_buf.pop_back();
                break;
            case KEY_ENTER: case 10: case 13:
                if (focus == 1) {
                    try {
                        result_line = std::stoi(line_buf);
                        if (result_line < 1) result_line = 1;
                        if (result_line > max_lines) result_line = max_lines;
                    } catch (...) { result_line = -1; }
                    pressed = true;
                } else if (focus == 2) {
                    pressed = true;
                }
                break;
            default:
                if (focus == 0 && isdigit(ch)) {
                    line_buf += (char)ch;
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
    return result_line;
}
