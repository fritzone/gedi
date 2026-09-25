#include "AboutDialog.h"
#include "curses_compat.h"
#include <vector>
#include <string>

namespace {

// Product identity shown in the box.
const char* kProductName = "Garland Turbine";
const char* kVersion     = "Version 1.0";

// Centred text block (below the logo). Blank strings render as spacer rows.
const std::vector<std::string> kLines = {
    "An integrated development environment",
    "for the C++ programmer.",
    "",
    "(c) fritzone 2026",
};

// Logo footprint, in character cells. The engine fits the image into this box
// preserving its aspect ratio, so these are an upper bound rather than an exact
// size. Roughly square to match the ~1:1 logo. Only the SDL build actually draws
// it (has_image is always false otherwise), but the layout math references these.
constexpr int LOGO_CW = 24;
constexpr int LOGO_CH = 11;

} // namespace

void AboutDialog::show(Renderer& renderer, const std::string& image_path) {
    int w = 0, h = 0, starty = 0, startx = 0, btn_y = 0, btn_x = 0;
    int text_top = 0;
    WINDOW* behind = nullptr;

    const std::string ok_text = " &Ok ";

    // Whether a logo will actually be drawn. In the text build: never. In the SDL
    // build: only if the engine manages to load the image (a probe load below).
    bool has_image = false;
#ifdef GEDI_GUI
    // Probe: try to load the logo now so the layout can reserve space only when it
    // will really be shown. gui_show_image_overlay is idempotent - it is called
    // again with the final position once the box is laid out.
    if (!image_path.empty())
        has_image = gui_show_image_overlay(image_path.c_str(), 0, 0, LOGO_CW, LOGO_CH,
                                           Renderer::CP_DIALOG, Renderer::CP_DIALOG_TITLE) != 0;
    if (!has_image)
        gui_hide_image_overlay();
#endif

    auto layoutAndDraw = [&]() {
        w = std::min(renderer.getWidth() - 4, 48);
        if (w < 30) w = 30;

        const int n = (int)kLines.size();
        const int logo_block = has_image ? (LOGO_CH + 1) : 0;   // logo rows + 1 spacer

        // border + blank + [logo + blank] + name + version + blank + text
        //        + blank + button + shadow + border
        int header = 2;                       // top border + 1 blank
        int name_rows = 2;                    // product name + version
        h = header + logo_block + name_rows + 1 + n + 1 + 3;

        starty = (renderer.getHeight() - h) / 2;
        startx = (renderer.getWidth()  - w) / 2;
        if (starty < 0) starty = 0;
        if (startx < 0) startx = 0;

        // Save what is behind the dialog so it can be restored on close.
        if (behind) delwin(behind);
        behind = newwin(h + 1, w + 1, starty, startx);
        if (behind)
            copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

        renderer.drawShadow(startx, starty, w, h);
        renderer.drawBoxWithTitle(startx, starty, w, h,
                                  Renderer::CP_DIALOG, Renderer::DOUBLE,
                                  " About ", Renderer::CP_DIALOG_TITLE, A_BOLD);

        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        for (int i = 1; i < h - 1; ++i)
            mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        // Centre a string on a dialog row.
        auto centre = [&](int row, const std::string& s, int flags) {
            int x = startx + (w - (int)s.size()) / 2;
            renderer.drawText(x, row, s, Renderer::CP_DIALOG, flags);
        };

        int row = starty + 2;               // first content row
        if (has_image) row += LOGO_CH + 1;  // leave the logo area (drawn as overlay)

        centre(row++, kProductName, A_BOLD);
        centre(row++, kVersion, 0);
        text_top = row + 1;                 // one blank line before the text block
        for (int i = 0; i < n; ++i)
            if (!kLines[i].empty()) centre(text_top + i, kLines[i], 0);

        btn_y = starty + h - 3;
        btn_x = startx + (w - (int)ok_text.size()) / 2;

#ifdef GEDI_GUI
        // Place the logo, centred horizontally, just below the top border.
        if (has_image) {
            int logo_cx = startx + (w - LOGO_CW) / 2;
            int logo_cy = starty + 2;
            gui_show_image_overlay(image_path.c_str(), logo_cx, logo_cy, LOGO_CW, LOGO_CH,
                                   Renderer::CP_DIALOG, Renderer::CP_DIALOG_TITLE);
        }
#endif
    };

    layoutAndDraw();

    nodelay(stdscr, FALSE);
    bool pressed = false;
    bool btn_captured = false;
    bool hover_pressed = false;
    while (true) {
        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        mvwaddstr(stdscr, btn_y,     startx + 1, std::string(w - 2, ' ').c_str());
        mvwaddstr(stdscr, btn_y + 1, startx + 1, std::string(w - 2, ' ').c_str());
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        renderer.drawButton(btn_x, btn_y, ok_text, true, pressed || hover_pressed);
        renderer.refresh();

        if (pressed) { napms(100); break; }

        wint_t ch = renderer.getChar();
        if (ch == KEY_RESIZE) {
            renderer.updateDimensions();
            renderer.repaintBackground();
            layoutAndDraw();
            continue;
        }
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) != OK) continue;

            bool is_press   = (ev.bstate & BUTTON1_PRESSED)   != 0;
            bool is_release = (ev.bstate & BUTTON1_RELEASED)  != 0;
            bool is_clicked = (ev.bstate & BUTTON1_CLICKED)   != 0;

            auto overOK = [&] {
                return ev.y == btn_y && ev.x >= btn_x && ev.x < btn_x + (int)ok_text.size();
            };
            auto inside = [&] {
                return ev.x >= startx && ev.x < startx + w &&
                       ev.y >= starty && ev.y < starty + h;
            };

            if (btn_captured) {
                if (is_release || is_clicked) {
                    if (overOK()) pressed = true;
                    btn_captured  = false;
                    hover_pressed = false;
                } else {
                    hover_pressed = overOK();
                }
            } else if (is_press) {
                if (overOK()) { btn_captured = true; hover_pressed = true; }
                else if (!inside()) pressed = true;   // click-away → dismiss
            } else if (is_clicked && overOK()) {
                pressed = true;
            }
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

#ifdef GEDI_GUI
    gui_hide_image_overlay();
#endif

    if (behind) {
        copywin(behind, stdscr, 0, 0, starty, startx, starty + h, startx + w, FALSE);
        delwin(behind);
    }
    nodelay(stdscr, TRUE);
    renderer.showCursor();
}
