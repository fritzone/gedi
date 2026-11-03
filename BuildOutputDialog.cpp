#include "BuildOutputDialog.h"
#include "BuildSystem.h"
#include "curses_compat.h"
#include "ScrollBar.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace {

// What a running build shares with the window showing it.
struct LiveBuild {
    std::mutex               m;
    std::vector<std::string> lines;          // streamed so far (guarded by m)
    std::vector<std::string> final_lines;    // the complete output, once done
    bool                     done = false;   // guarded by m
    std::atomic<bool>        cancel{false};
};

int visualLen(const std::string& label) {
    int n = 0;
    for (char c : label) if (c != '&') ++n;
    return n;
}

// The window, for a finished build (live == nullptr) or a running one. A
// running build's window returns when the build is done, unless keep_open
// says to stay on its output.
void runWindow(Renderer& renderer, std::vector<std::string> lines,
               LiveBuild* live, const std::function<bool()>* keep_open)
{
    int h = 0, w = 0, starty = 0, startx = 0, visible_h = 0;
    WINDOW* behind = nullptr;
    auto layout = [&] {
        h = std::max(15, renderer.getHeight() - 10);
        w = std::max(60, renderer.getWidth() - 20);
        starty = std::max(0, (renderer.getHeight() - h) / 2);
        startx = std::max(0, (renderer.getWidth()  - w) / 2);
        visible_h = h - 6;
        behind = newwin(h + 1, w + 1, starty, startx);
        if (behind) copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);
        renderer.drawShadow(startx, starty, w, h);
    };
    layout();

    const auto started = std::chrono::steady_clock::now();
    bool running  = live != nullptr;
    bool follow   = live != nullptr;      // keep the newest output in view
    int scroll_pos = 0;
    bool pressed = false;                 // the button: Close, or Cancel while running
    bool btn_captured = false;            // mouse button went down on it
    bool hover_pressed = false;           // ...and is still over it
    ScrollBar bar;                        // on the frame's right edge, beside the output

    auto maxScroll = [&] { return std::max(0, (int)lines.size() - visible_h); };
    auto scrollTo = [&](int t) {
        scroll_pos = std::clamp(t, 0, maxScroll());
        follow = running && scroll_pos >= maxScroll();
    };
    auto requestCancel = [&] { if (live) live->cancel = true; };

    nodelay(stdscr, FALSE);
    while (true) {
        // New output, and whether the build has ended.
        if (running) {
            bool done;
            {
                std::lock_guard<std::mutex> lk(live->m);
                for (size_t i = lines.size(); i < live->lines.size(); ++i) lines.push_back(live->lines[i]);
                done = live->done;
                if (done) lines = live->final_lines;
            }
            if (done) {
                running = false;
                if (!keep_open || !(*keep_open)()) break;
            }
            if (follow || done) scroll_pos = maxScroll();
        }
        scroll_pos = std::clamp(scroll_pos, 0, maxScroll());

        std::string title = " Build Output ";
        if (running) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            static const char spin[] = "|/-\\";
            title = std::string(" Building ") + spin[(ms / 150) % 4] + " " +
                    std::to_string(ms / 1000) + "s ";
        }
        renderer.drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::DOUBLE,
                                  title, Renderer::CP_DIALOG_TITLE, A_BOLD);
        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        for (int i = 1; i < h - 1; ++i) mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        for (int i = 0; i < visible_h && scroll_pos + i < (int)lines.size(); ++i) {
            // Build commands are far wider than any dialog; show a summary
            // that fits instead of clipping their informative tail off.
            std::string line = BuildSystem::summarizeOutputLine(lines[scroll_pos + i], (size_t)(w - 4));
            if (line.length() > (size_t)w - 4) line = line.substr(0, w - 4);
            renderer.drawText(startx + 2, starty + 2 + i, line, Renderer::CP_DIALOG);
        }

        bar.layout(startx + w - 1, starty + 2, visible_h, true, (int)lines.size(), visible_h, scroll_pos);
        const bool has_bar = bar.needed() && visible_h >= 3;
        if (has_bar) bar.draw(renderer, Renderer::CP_DIALOG);

        const bool stopping = running && live->cancel;
        const std::string label = !running ? " &Close " : stopping ? " Stopping... " : " &Cancel ";
        const int btn_x = startx + (w - visualLen(label)) / 2;
        const int btn_y = starty + h - 3;
        renderer.drawButton(btn_x, btn_y, label, !stopping, pressed || hover_pressed);
        renderer.refresh();

        if (pressed) {
            napms(100);
            pressed = false;
            hover_pressed = false;
            if (running) requestCancel();   // the window stays until the build has stopped
            else break;
            continue;
        }

        // While the build runs, look for new output every tenth of a second.
        timeout(running ? 100 : -1);
        wint_t ch = renderer.getChar();
        if (ch == (wint_t)ERR) continue;

        if (ch == KEY_MOUSE) {
            // Always drain the mouse-event queue, even for events we don't act
            // on - otherwise unconsumed events pile up while this dialog is
            // open and desync every click the editor receives after it closes.
            MEVENT ev;
            if (getmouse(&ev) != OK) continue;

            const bool is_press     = (ev.bstate & BUTTON1_PRESSED)  != 0;
            const bool is_release   = (ev.bstate & BUTTON1_RELEASED) != 0;
            const bool is_clicked   = (ev.bstate & BUTTON1_CLICKED)  != 0;
            const bool is_scroll_up = (ev.bstate & BUTTON4_PRESSED)  != 0;
            const bool is_scroll_dn = (ev.bstate & BUTTON5_PRESSED)  != 0;
            const bool pos_rpt = (ev.bstate & REPORT_MOUSE_POSITION) != 0;
            const bool motion  = (ev.bstate & (BUTTON1_MOTION | REPORT_MOUSE_POSITION)) != 0;
            auto overButton = [&] {
                return !stopping && ev.y == btn_y && ev.x >= btn_x && ev.x < btn_x + visualLen(label);
            };

            int t;
            if (has_bar && !btn_captured && ((is_press && !pos_rpt) || bar.dragging()) &&
                bar.mouse(ev.x, ev.y, is_press && !pos_rpt, motion, is_release || is_clicked, t)) {
                scrollTo(t);
            }
            else if (is_scroll_up || is_scroll_dn) scrollTo(scroll_pos + (is_scroll_up ? -3 : 3));
            else if (btn_captured) {
                if (is_release || is_clicked) {
                    if (overButton()) pressed = true;
                    btn_captured  = false;
                    hover_pressed = false;
                } else {
                    hover_pressed = overButton();
                }
            } else if (is_press) {
                if (overButton()) { btn_captured = true; hover_pressed = true; }
            } else if (is_clicked && overButton()) {
                pressed = true;
            }
            continue;
        }
        if (ch == KEY_RESIZE) {
            // Screen was blanked and resized - recompute geometry, recapture the
            // backdrop and repaint the frame so no stale garbage is left behind.
            renderer.updateDimensions();
            renderer.repaintBackground();   // redraw the editor behind at the new size
            if (behind) delwin(behind);
            layout();
            continue;
        }
        if      (ch == KEY_UP)    scrollTo(scroll_pos - 1);
        else if (ch == KEY_DOWN)  scrollTo(scroll_pos + 1);
        else if (ch == KEY_PPAGE) scrollTo(scroll_pos - visible_h);
        else if (ch == KEY_NPAGE) scrollTo(scroll_pos + visible_h);
        else if (ch == KEY_HOME)  scrollTo(0);
        else if (ch == KEY_END)   scrollTo(maxScroll());
        else if (ch == 27 || ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13 || tolower(ch) == 'c') {
            // Close; while the build runs, Cancel (Escape included).
            if (!stopping) pressed = true;
        }
    }

    timeout(-1);
    if (behind) {
        copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE);
        delwin(behind);
    }
    nodelay(stdscr, TRUE);
    renderer.showCursor();
}

} // namespace

void BuildOutputDialog::show(Renderer& renderer, const std::vector<std::string>& lines) {
    runWindow(renderer, lines, nullptr, nullptr);
}

void BuildOutputDialog::runLive(Renderer& renderer,
                                const std::function<std::vector<std::string>(const BuildMonitor&)>& work,
                                const std::function<bool()>& keep_open)
{
    LiveBuild live;
    std::thread worker([&] {
        BuildMonitor monitor;
        monitor.on_line = [&](const std::string& ln) {
            std::lock_guard<std::mutex> lk(live.m);
            live.lines.push_back(ln);
        };
        monitor.cancel = &live.cancel;
        std::vector<std::string> out;
        try {
            out = work(monitor);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(live.m);
            out = live.lines;
            out.push_back(std::string("[the build stopped with an error: ") + e.what() + "]");
        }
        std::lock_guard<std::mutex> lk(live.m);
        live.final_lines = std::move(out);
        live.done = true;
    });
    runWindow(renderer, {}, &live, &keep_open);
    worker.join();
}
