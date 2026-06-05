#include "MessageDialog.h"
#include "curses_compat.h"
#include <vector>
#include <string>
#include <algorithm>

// Word-wrap a single paragraph (no embedded newlines) to lines of at most max_w chars.
static std::vector<std::string> wrapParagraph(const std::string& para, int max_w)
{
    std::vector<std::string> out;
    std::string remaining = para;
    while ((int)remaining.size() > max_w) {
        int cut = max_w;
        // Walk back to find a space to break on
        while (cut > 0 && remaining[cut] != ' ') --cut;
        if (cut == 0) cut = max_w;   // no space found — hard cut
        out.push_back(remaining.substr(0, cut));
        remaining = remaining.substr(remaining[cut] == ' ' ? cut + 1 : cut);
    }
    out.push_back(remaining);
    return out;
}

// Split on '\n' then word-wrap each paragraph.
static std::vector<std::string> wrapMessage(const std::string& message, int max_w)
{
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= message.size()) {
        size_t nl  = message.find('\n', pos);
        size_t end = (nl == std::string::npos) ? message.size() : nl;
        std::string para = message.substr(pos, end - pos);
        auto wrapped = wrapParagraph(para, max_w);
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
        pos = end + 1;
        if (nl == std::string::npos) break;
    }
    return lines;
}

void MessageDialog::show(Renderer& renderer, const std::string& message) {
    // ── Layout state (recomputed on resize) ──────────────────────────────────
    int w = 0, h = 0, n = 0, starty = 0, startx = 0, btn_y = 0, btn_x = 0;
    std::vector<std::string> lines;
    WINDOW* behind = nullptr;

    const std::string ok_text = " &Ok ";

    // Size the dialog to the content and the current screen, snapshot the area
    // behind it, and draw the frame + wrapped text. Called once up front and
    // again on every resize so nothing stale is left on screen.
    auto layoutAndDraw = [&]() {
        const int max_w   = std::min(renderer.getWidth() - 8, 60);
        const int inner_w = max_w - 4;   // 2 border + 2 padding on each side

        lines = wrapMessage(message, inner_w);

        // Cap lines so the dialog is never taller than the screen
        const int max_lines = renderer.getHeight() - 8;
        if ((int)lines.size() > max_lines) lines.resize(std::max(0, max_lines));

        n = (int)lines.size();
        w = max_w;
        // Layout: border(1) + margin(1) + text(n) + margin(1) + button(1) + shadow(1) + border(1)
        h = n + 6;

        starty = (renderer.getHeight() - h) / 2;
        startx = (renderer.getWidth()  - w) / 2;
        if (starty < 0) starty = 0;
        if (startx < 0) startx = 0;

        // ── Save area behind dialog ───────────────────────────────────────────
        if (behind) delwin(behind);
        behind = newwin(h + 1, w + 1, starty, startx);
        if (behind)
            copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

        // ── Draw frame ────────────────────────────────────────────────────────
        renderer.drawShadow(startx, starty, w, h);
        renderer.drawBoxWithTitle(startx, starty, w, h,
                                  Renderer::CP_DIALOG, Renderer::DOUBLE,
                                  " Message ", Renderer::CP_DIALOG_TITLE, A_BOLD);

        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        for (int i = 1; i < h - 1; ++i)
            mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        // ── Draw wrapped text (one row below the top border + margin) ──────────
        for (int i = 0; i < n; ++i)
            renderer.drawText(startx + 2, starty + 2 + i, lines[i], Renderer::CP_DIALOG);

        btn_y = starty + h - 3;   // leaves room for shadow + bottom border
        btn_x = startx + (w - (int)ok_text.size()) / 2;
    };

    layoutAndDraw();

    nodelay(stdscr, FALSE);
    bool pressed = false;
    while (true) {
        // Clear button + shadow rows before each draw so the pressed shift
        // doesn't leave a ghost of the previous unpressed button behind.
        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        mvwaddstr(stdscr, btn_y,     startx + 1, std::string(w - 2, ' ').c_str());
        mvwaddstr(stdscr, btn_y + 1, startx + 1, std::string(w - 2, ' ').c_str());
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        renderer.drawButton(btn_x, btn_y, ok_text, true, pressed);
        renderer.refresh();

        if (pressed) { napms(100); break; }

        wint_t ch = renderer.getChar();
        if (ch == KEY_RESIZE) {
            renderer.updateDimensions();
            renderer.repaintBackground();   // redraw the editor behind at the new size
            layoutAndDraw();   // re-centre, re-wrap and repaint at the new size
            continue;
        }
        if (ch == 27) {
            timeout(1);
            wint_t next = renderer.getChar();
            timeout(-1);
            if (next == (wint_t)ERR) break;
        }
        if (ch == KEY_ENTER || ch == 10 || ch == 13 || ch == ' ' || tolower(ch) == 'o')
            pressed = true;
    }

    // ── Restore ───────────────────────────────────────────────────────────────
    if (behind) {
        copywin(behind, stdscr, 0, 0, starty, startx, starty + h, startx + w, FALSE);
        delwin(behind);
    }
    nodelay(stdscr, TRUE);
    renderer.showCursor();
}
