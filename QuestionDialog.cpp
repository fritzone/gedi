#include "QuestionDialog.h"
#include <ncurses.h>
#include <algorithm>

int QuestionDialog::ask(Renderer& renderer, const std::string& question, const std::string& info) {
    renderer.hideCursor();

    int h = 8, w = 54;
    int starty = (renderer.getHeight() - h) / 2;
    int startx = (renderer.getWidth() - w) / 2;

    std::string display_info = info;
    int max_text_width = w - 6;
    if (display_info.length() > (size_t)max_text_width) {
        display_info = "..." + display_info.substr(display_info.length() - (max_text_width - 3));
    }

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

    renderer.drawShadow(startx, starty, w, h);
    renderer.drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::DOUBLE, " Question ", Renderer::CP_DIALOG_TITLE, A_BOLD);

    wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
    for (int i = 1; i < h - 1; ++i) {
        mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
    }
    wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

    renderer.drawText(startx + 2, starty + 2, question, Renderer::CP_DIALOG);
    if (!display_info.empty()) {
        renderer.drawText(startx + 4, starty + 3, display_info, Renderer::CP_DIALOG, A_BOLD);
    }

    int selection = 0; // 0 for Yes, 1 for No
    nodelay(stdscr, FALSE);
    int final_result = -1;
    bool pressed = false;

    std::string yes_text = " &Yes ";
    std::string no_text = " &No ";
    int total_btn_width = yes_text.length() + no_text.length() + 4;
    int btn_y = starty + h - 3;
    int yes_x = startx + (w - total_btn_width) / 2;
    int no_x = yes_x + yes_text.length() + 4;

    while(true) {
        renderer.drawButton(yes_x, btn_y, yes_text, selection == 0, pressed && selection == 0);
        renderer.drawButton(no_x, btn_y, no_text, selection == 1, pressed && selection == 1);
        renderer.refresh();

        if (pressed) {
            napms(100);
            goto end_dialog;
        }

        wint_t ch = renderer.getChar();

        if (ch == 27) { // Alt key sequence or ESC
            timeout(1);
            wint_t next_ch = renderer.getChar();
            timeout(-1);
            if (next_ch != ERR) {
                if (tolower(next_ch) == 'y') { final_result = 1; selection = 0; pressed = true; }
                else if (tolower(next_ch) == 'n') { final_result = 0; selection = 1; pressed = true; }
            } else { // Just ESC
                final_result = -1; break;
            }
        } else {
            switch (ch) {
                case KEY_LEFT: case KEY_RIGHT: case 9: // Tab
                    selection = 1 - selection;
                    break;
                case 'y': case 'Y': final_result = 1; selection = 0; pressed = true; break;
                case 'n': case 'N': final_result = 0; selection = 1; pressed = true; break;
                case ' ': case KEY_ENTER: case 10: case 13:
                    final_result = (selection == 0) ? 1 : 0;
                    pressed = true;
                    break;
            }
        }
    }

end_dialog:
    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE);
    delwin(behind);
    nodelay(stdscr, TRUE);
    renderer.showCursor();
    return final_result;
}
