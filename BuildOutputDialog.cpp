#include "BuildOutputDialog.h"
#include "curses_compat.h"
#include <algorithm>




void BuildOutputDialog::show(Renderer& renderer, const std::vector<std::string>& lines) {
    int h = renderer.getHeight() - 10;
    int w = renderer.getWidth() - 20;
    if (h < 15) h = 15;
    if (w < 60) w = 60;
    int starty = (renderer.getHeight() - h) / 2;
    int startx = (renderer.getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

    renderer.drawShadow(startx, starty, w, h);
    renderer.drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::DOUBLE, " Build Output ", Renderer::CP_DIALOG_TITLE, A_BOLD);

    int scroll_pos = 0;
    int visible_h = h - 6;
    nodelay(stdscr, FALSE);

    bool pressed = false;
    while (true) {
        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        for (int i = 1; i < h - 1; ++i) mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        for (int i = 0; i < visible_h; ++i) {
            if (scroll_pos + i < (int)lines.size()) {
                std::string line = lines[scroll_pos + i];
                if (line.length() > (size_t)w - 4) line = line.substr(0, w - 4);
                renderer.drawText(startx + 2, starty + 2 + i, line, Renderer::CP_DIALOG);
            }
        }

        renderer.drawButton(startx + (w - 10) / 2, starty + h - 3, " &Close ", true, pressed);
        renderer.refresh();

        if (pressed) {
            napms(100);
            break;
        }

        wint_t ch = renderer.getChar();
        if (ch == KEY_RESIZE) {
            // Screen was blanked and resized — recompute geometry, recapture the
            // backdrop and repaint the frame so no stale garbage is left behind.
            renderer.updateDimensions();
            renderer.repaintBackground();   // redraw the editor behind at the new size
            if (behind) delwin(behind);
            h = renderer.getHeight() - 10;
            w = renderer.getWidth() - 20;
            if (h < 15) h = 15;
            if (w < 60) w = 60;
            starty = (renderer.getHeight() - h) / 2;
            startx = (renderer.getWidth()  - w) / 2;
            if (starty < 0) starty = 0;
            if (startx < 0) startx = 0;
            visible_h = h - 6;
            if (scroll_pos + visible_h > (int)lines.size())
                scroll_pos = std::max(0, (int)lines.size() - visible_h);
            behind = newwin(h + 1, w + 1, starty, startx);
            if (behind) copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);
            renderer.drawShadow(startx, starty, w, h);
            renderer.drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::DOUBLE, " Build Output ", Renderer::CP_DIALOG_TITLE, A_BOLD);
            continue;
        }
        if (ch == KEY_UP) { if (scroll_pos > 0) scroll_pos--; }
        else if (ch == KEY_DOWN) { if (scroll_pos + visible_h < (int)lines.size()) scroll_pos++; }
        else if (ch == KEY_PPAGE) { scroll_pos -= visible_h; if (scroll_pos < 0) scroll_pos = 0; }
        else if (ch == KEY_NPAGE) { scroll_pos += visible_h; if (scroll_pos + visible_h > (int)lines.size()) scroll_pos = std::max(0, (int)lines.size() - visible_h); }
        else if (ch == 27 || ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13 || tolower(ch) == 'c') pressed = true;
    }

    if (behind) {
        copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE);
        delwin(behind);
    }
    nodelay(stdscr, TRUE);
    
    renderer.showCursor();
}
