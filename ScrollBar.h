#pragma once
#include "curses_compat.h"
#include "Renderer.h"
#include <algorithm>

// Not every curses defines it (the terminal build reports drags as position
// reports); the same fallback as the editor's.
#ifndef BUTTON1_MOTION
#  define BUTTON1_MOTION static_cast<mmask_t>(0x20)
#endif

// ═══════════════════════════════════════════════════════════════════════════════
// ScrollBar - the one scrollbar of the program
//
// Every scrolling view (editor, combo lists, Compile Options, help, the file
// browser, the output window, the debug panel...) draws and drives its bar
// with this, so they all look and behave alike:
//
//   vertical     ▲          horizontal   ◄▒▒▒███▒▒▒▒►
//                ▒
//                █  thumb: where the view is, sized by how much it shows
//                ▒
//                ▼
//
//   click ▲ / ▼ (◄ / ►)          scroll one line (column)
//   click the track beside the   scroll one page towards that side
//     thumb
//   drag the thumb               scroll along with it (for views that pass
//                                motion and release events)
//
// It usually sits on the right (bottom) edge of its view's frame and is drawn
// in the frame's colours. A view keeps one ScrollBar, sets its geometry and
// content numbers when it draws (layout()), and gives it its mouse events
// (mouse()); the bar keeps the drag state between them.
// ═══════════════════════════════════════════════════════════════════════════════
struct ScrollBar {
    int  x = 0, y = 0;       // screen cell of the first arrow (▲ or ◄)
    int  length  = 0;        // cells, both arrows included
    bool vertical = true;
    int  total   = 0;        // lines (columns) of content
    int  visible = 0;        // how many of them the view shows at once
    int  pos     = 0;        // the first one shown

    void layout(int x_, int y_, int length_, bool vertical_,
                int total_, int visible_, int pos_) {
        x = x_; y = y_; length = length_; vertical = vertical_;
        total = total_; visible = std::max(1, visible_); pos = pos_;
    }

    // More content than fits - the views that show a bar only when there is
    // something to scroll ask this.
    bool needed()  const { return total > visible; }
    int  maxPos()  const { return std::max(0, total - visible); }
    int  track()   const { return length - 2; }

    // Where the thumb is in the track (cells after the first arrow) and how
    // long. Content that fits fills the whole track.
    void thumb(int& tpos, int& tlen) const {
        const int t = track();
        if (t <= 0) { tpos = 0; tlen = 0; return; }
        if (!needed()) { tpos = 0; tlen = t; return; }
        tlen = std::clamp(t * visible / std::max(1, total), 1, t);
        const int range = maxPos();
        const int p = std::clamp(pos, 0, range);
        tpos = range > 0 ? ((t - tlen) * p + range / 2) / range : 0;
    }

    void draw(Renderer& r, int color) const {
        if (length < 2) return;
        auto cell = [&](int i, const char* glyph, int attr) {
            if (vertical) r.drawText(x, y + i, glyph, color, attr);
            else          r.drawText(x + i, y, glyph, color, attr);
        };
        cell(0,          vertical ? "\xe2\x96\xb2" : "\xe2\x97\x84", A_BOLD);   // ▲ / ◄
        cell(length - 1, vertical ? "\xe2\x96\xbc" : "\xe2\x96\xba", A_BOLD);   // ▼ / ►
        int tpos, tlen;
        thumb(tpos, tlen);
        for (int i = 0; i < track(); ++i) {
            const bool on_thumb = i >= tpos && i < tpos + tlen;
            cell(1 + i, on_thumb ? "\xe2\x96\x88" : "\xe2\x96\x92", A_NORMAL);  // █ / ▒
        }
    }

    // Is (mx, my) on the bar?
    bool contains(int mx, int my) const {
        if (length < 2) return false;
        return vertical ? (mx == x && my >= y && my < y + length)
                        : (my == y && mx >= x && mx < x + length);
    }

    bool dragging() const { return drag_grab_ >= 0; }
    void cancelDrag()     { drag_grab_ = -1; }

    // A mouse event. press: left button went down; motion: the pointer moved
    // with it held; release: it came up. Returns true when the event was the
    // bar's - new_pos then holds the first line (column) to show, which may be
    // unchanged (a click on the thumb, an arrow at the end).
    bool mouse(int mx, int my, bool press, bool motion, bool release, int& new_pos) {
        new_pos = pos;
        if (dragging()) {
            if (release) { cancelDrag(); return true; }
            if (motion || press) { new_pos = posForThumbAt(cellOf(mx, my) - 1 - drag_grab_); return true; }
            return false;
        }
        if (!press || !contains(mx, my)) return false;
        const int c = cellOf(mx, my);
        int tpos, tlen;
        thumb(tpos, tlen);
        if (c == 0)                         new_pos = pos - 1;
        else if (c == length - 1)           new_pos = pos + 1;
        else if (!needed())                 new_pos = pos;
        else if (c - 1 < tpos)              new_pos = pos - visible;
        else if (c - 1 >= tpos + tlen)      new_pos = pos + visible;
        else                                drag_grab_ = c - 1 - tpos;   // on the thumb
        new_pos = std::clamp(new_pos, 0, maxPos());
        return true;
    }

private:
    int drag_grab_ = -1;     // while dragging: where in the thumb it was taken

    int cellOf(int mx, int my) const { return vertical ? my - y : mx - x; }

    // The first line to show when the thumb starts at track cell `tpos`.
    int posForThumbAt(int tpos) const {
        int tp, tlen;
        thumb(tp, tlen);
        const int free_cells = track() - tlen;
        if (free_cells <= 0) return pos;
        tpos = std::clamp(tpos, 0, free_cells);
        return (maxPos() * tpos + free_cells / 2) / free_cells;
    }
};
