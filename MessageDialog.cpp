#include "MessageDialog.h"
#include <ncurses.h>

void MessageDialog::show(Renderer& renderer, const std::string& message) {
    int h = 8, w = 42;
    int starty = (renderer.getHeight() - h) / 2;
    int startx = (renderer.getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

    renderer.drawShadow(startx, starty, w, h);
    renderer.drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::DOUBLE, " Message ", Renderer::CP_DIALOG_TITLE, A_BOLD);
    
    // Clear background
    wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
    for (int i = 1; i < h - 1; ++i) {
        mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
    }
    wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

    // Split and draw message
    std::vector<std::string> lines;
    size_t last = 0, next = 0;
    while ((next = message.find('\n', last)) != std::string::npos) {
        lines.push_back(message.substr(last, next - last));
        last = next + 1;
    }
    lines.push_back(message.substr(last));

    for (size_t i = 0; i < lines.size() && i < (size_t)h - 4; ++i) {
        renderer.drawText(startx + 2, starty + 3 + i, lines[i], Renderer::CP_DIALOG);
    }

    std::string ok_text = " &Ok ";
    nodelay(stdscr, FALSE);
    wint_t ch;
    bool pressed = false;
    while (true) {
        renderer.drawButton(startx + (w - ok_text.length()) / 2, starty + h - 3, ok_text, true, pressed);
        renderer.refresh();
        
        if (pressed) {
            napms(100);
            break;
        }

        ch = renderer.getChar();
        if (ch == 27) { // ESC
            timeout(1);
            wint_t next_ch = renderer.getChar();
            timeout(-1);
            if (next_ch == ERR) break;
        }
        if (ch == KEY_ENTER || ch == 10 || ch == 13 || ch == ' ' || tolower(ch) == 'o') {
            pressed = true;
        }
    }

    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE);
    delwin(behind);
    nodelay(stdscr, TRUE);
    renderer.showCursor();
}
