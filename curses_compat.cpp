// curses_compat.cpp
//
// Implementation of the ncurses emulation layer used by the graphical (SDL)
// build of gedi.  Every drawing operation ends up in the cell grid of `stdscr`
// (or another emulated window); refresh() flushes that grid to the SDL based
// BorlandEngine VGA text-mode emulator.  Keyboard and mouse events coming from
// SDL are translated into the exact key codes / MEVENT values the rest of the
// editor already expects from ncurses.
#ifdef GEDI_GUI

#include "curses_compat.h"
#include "graphics_engine/BorlandEngine.h"
#include "graphics_engine/Constants.h"

#include <SDL2/SDL.h>
#include <deque>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>

// ---------------------------------------------------------------------------
//  Global emulator state
// ---------------------------------------------------------------------------
static BorlandEngine  g_eng;
WINDOW*               stdscr = nullptr;
int                   COLS   = 80;
int                   LINES  = 25;

static bool           g_cursor_visible = true;
static bool           g_nodelay        = true;   // stdscr starts non-blocking
static int            g_timeout        = -1;     // -1 block, 0 nonblock, >0 ms

static std::deque<wint_t> g_input;   // pending key codes
static std::deque<MEVENT> g_mouse;   // pending mouse events, paired with KEY_MOUSE
static bool           g_btn_down     = false;    // left mouse button held?
static bool           g_mouse_inside = true;
static int            g_last_motion_gx = -1;      // last reported hover grid cell
static int            g_last_motion_gy = -1;

// colour-pair table: pair number -> (ncurses fg, ncurses bg)
static short g_pair_fg[256];
static short g_pair_bg[256];

// ncurses colour number (0-15) -> Borland/VGA palette index
static const int kNcursesToVga[16] = {
    0 /*black*/,   4 /*red*/,    2 /*green*/,  6 /*brown(yellow)*/,
    1 /*blue*/,    5 /*magenta*/,3 /*cyan*/,   7 /*lightgray(white)*/,
    8 /*darkgray*/,12/*lightred*/,10/*lightgreen*/,14/*yellow*/,
    9 /*lightblue*/,13/*lightmagenta*/,11/*lightcyan*/,15/*white*/
};

// ---------------------------------------------------------------------------
//  Unicode -> CP437 mapping (only the glyphs gedi actually draws)
// ---------------------------------------------------------------------------
static unsigned char uni_to_cp437(wchar_t c) {
    if (c >= 0x20 && c < 0x7f) return (unsigned char)c;   // plain ASCII
    switch (c) {
        // single line box
        case 0x250C: return 0xDA; case 0x2510: return 0xBF;
        case 0x2514: return 0xC0; case 0x2518: return 0xD9;
        case 0x251C: return 0xC3; case 0x2524: return 0xB4;
        case 0x252C: return 0xC2; case 0x2534: return 0xC1;
        case 0x2500: return 0xC4; case 0x2502: return 0xB3;
        case 0x253C: return 0xC5;
        // double line box
        case 0x2554: return 0xC9; case 0x2557: return 0xBB;
        case 0x255A: return 0xC8; case 0x255D: return 0xBC;
        case 0x2560: return 0xCC; case 0x2563: return 0xB9;
        case 0x2566: return 0xCB; case 0x2569: return 0xCA;
        case 0x2550: return 0xCD; case 0x2551: return 0xBA;
        case 0x256C: return 0xCE;
        // single/double tee mixes used by the editor frame
        case 0x2564: return 0xD1; case 0x2567: return 0xCF;
        case 0x2565: return 0xD2; case 0x2568: return 0xD0;
        // blocks & shades
        case 0x2588: return 0xDB; case 0x2580: return 0xDF; case 0x2584: return 0xDC;
        case 0x258C: return 0xDD; case 0x2590: return 0xDE;
        case 0x2591: return 0xB0; case 0x2592: return 0xB1; case 0x2593: return 0xB2;
        // misc
        case 0x00B7: return 0xFA; // middle dot (show-whitespace space marker)
        case 0x00B0: return 0xF8; // degree
        case 0x2022: return 0x07; // bullet
        case 0x2190: return 0x1B; // left arrow
        case 0x2192: return 0x1A; // right arrow
        case 0x2191: return 0x18; // up arrow
        case 0x2193: return 0x19; // down arrow
        case 0x2026: return 0xFA; // ellipsis -> dot
        case 0x1FB98: return 0xB1;// hatched block (whitespace tab marker)
    }
    if (c < 0x80) return (unsigned char)c;
    return (unsigned char)'?';
}

// ---------------------------------------------------------------------------
//  Window helpers
// ---------------------------------------------------------------------------
static inline bool win_inside(WINDOW* w, int y, int x) {
    return w && y >= 0 && x >= 0 && y < w->rows && x < w->cols;
}
static inline CursesCell& win_cell(WINDOW* w, int y, int x) {
    return w->cells[(size_t)y * w->cols + x];
}

static void win_resize(WINDOW* w, int rows, int cols, chtype fill_attr) {
    w->rows = rows; w->cols = cols;
    w->cells.assign((size_t)rows * cols, CursesCell{L' ', fill_attr});
}

// put one code point honouring the window's current attribute
static void win_put(WINDOW* w, int y, int x, wchar_t ch, chtype attr) {
    if (!win_inside(w, y, x)) return;
    CursesCell& cell = win_cell(w, y, x);
    cell.ch = ch;
    cell.attr = attr;
}

// ---------------------------------------------------------------------------
//  Flush stdscr to the BorlandEngine and present a frame
// ---------------------------------------------------------------------------
static void pump_sdl();   // fwd

static void blit_and_present() {
    if (!stdscr) return;
    int rows = std::min(stdscr->rows, g_eng.SCREEN_ROWS);
    int cols = std::min(stdscr->cols, g_eng.SCREEN_COLS);
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            const CursesCell& cell = win_cell(stdscr, y, x);
            short pair = (short)PAIR_NUMBER(cell.attr);
            int nf = g_pair_fg[pair & 0xff];
            int nb = g_pair_bg[pair & 0xff];
            int fg = kNcursesToVga[nf & 0x0f];
            int bg = kNcursesToVga[nb & 0x0f];
            if (cell.attr & A_BOLD)    fg |= 8;
            if (cell.attr & A_DIM)     fg &= 7;
            if (cell.attr & A_REVERSE) { int t = fg; fg = bg; bg = t; }
            int idx = y * g_eng.SCREEN_COLS + x;
            if (idx >= 0 && idx < (int)g_eng.buffer.size())
                g_eng.buffer[idx] = { uni_to_cp437(cell.ch), (uint8_t)fg, (uint8_t)bg };
        }
    }
    if (g_cursor_visible)
        g_eng.set_hw_cursor(stdscr->curx + 1, stdscr->cury + 1);
    else
        g_eng.hide_hw_cursor();
    g_eng.render_frame();
}

// ---------------------------------------------------------------------------
//  SDL -> ncurses input translation
// ---------------------------------------------------------------------------
static void push_key(wint_t k)         { g_input.push_back(k); }
static void push_alt(wint_t base)      { g_input.push_back(27); g_input.push_back(base); }

static void push_mouse(int gx, int gy, mmask_t bstate) {
    MEVENT m{};
    m.x = gx - 1;            // BorlandEngine grid is 1-based, ncurses is 0-based
    m.y = gy - 1;
    m.bstate = bstate;
    g_mouse.push_back(m);
    g_input.push_back(KEY_MOUSE);
}

static void resize_to_engine() {
    if (!stdscr) return;
    win_resize(stdscr, g_eng.SCREEN_ROWS, g_eng.SCREEN_COLS, stdscr->bkgd);
    COLS  = g_eng.SCREEN_COLS;
    LINES = g_eng.SCREEN_ROWS;
}

static void translate_keydown(SDL_Keycode key, Uint16 mod) {
    const bool ctrl  = (mod & KMOD_CTRL)  != 0;
    const bool shift = (mod & KMOD_SHIFT) != 0;
    const bool alt   = (mod & KMOD_ALT)   != 0 && (mod & KMOD_RALT) == 0;

    // Zoom shortcuts handled by the emulator itself.
    if (ctrl && (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS)) {
        g_eng.zoomIn();  resize_to_engine(); push_key(KEY_RESIZE); return;
    }
    if (ctrl && (key == SDLK_MINUS || key == SDLK_KP_MINUS)) {
        g_eng.zoomOut(); resize_to_engine(); push_key(KEY_RESIZE); return;
    }

    switch (key) {
        case SDLK_UP:
            if (shift && ctrl) push_key(567);
            else if (ctrl)     push_key(566);
            else if (shift)    push_key(KEY_SR);
            else               push_key(KEY_UP);
            return;
        case SDLK_DOWN:
            if (shift && ctrl) push_key(526);
            else if (ctrl)     push_key(525);
            else if (shift)    push_key(KEY_SF);
            else               push_key(KEY_DOWN);
            return;
        case SDLK_LEFT:
            if (shift && ctrl) push_key(546);
            else if (ctrl)     push_key(545);
            else if (shift)    push_key(KEY_SLEFT);
            else               push_key(KEY_LEFT);
            return;
        case SDLK_RIGHT:
            if (shift && ctrl) push_key(561);
            else if (ctrl)     push_key(560);
            else if (shift)    push_key(KEY_SRIGHT);
            else               push_key(KEY_RIGHT);
            return;
        case SDLK_HOME:     push_key(shift ? KEY_SHOME : KEY_HOME); return;
        case SDLK_END:      push_key(shift ? KEY_SEND  : KEY_END);  return;
        case SDLK_PAGEUP:   push_key(shift ? KEY_SPREVIOUS : KEY_PPAGE); return;
        case SDLK_PAGEDOWN: push_key(shift ? KEY_SNEXT     : KEY_NPAGE); return;
        case SDLK_INSERT:   push_key(shift ? KEY_SIC : KEY_IC); return;
        case SDLK_DELETE:   push_key(shift ? KEY_SDC : KEY_DC); return;
        case SDLK_BACKSPACE:
            if (alt) push_alt(127);
            else     push_key(KEY_BACKSPACE);
            return;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: push_key(KEY_ENTER); return;
        case SDLK_TAB:      push_key(shift ? KEY_BTAB : 9); return;
        case SDLK_ESCAPE:   push_key(27); return;
        default: break;
    }

    // Function keys F1..F12 (+12 for shifted, like ncurses)
    if (key >= SDLK_F1 && key <= SDLK_F12) {
        int n = (int)(key - SDLK_F1) + 1;
        if (shift) n += 12;
        int code = KEY_F(n);
        if (alt)       push_alt(code);
        else if (ctrl) push_key(code & 0x1f);   // matches gedi's CTRL(KEY_F(9)) mapping
        else           push_key(code);
        return;
    }

    // Alt + printable letter / digit -> ESC prefixed sequence (terminal style)
    if (alt && key >= 0x20 && key < 0x7f) {
        push_alt((wint_t)key);
        return;
    }
    // Ctrl + letter -> control code; TEXTINPUT will not fire for these.
    if (ctrl && key >= SDLK_a && key <= SDLK_z) {
        push_key((wint_t)((key - SDLK_a + 1)));    // Ctrl+A == 1 .. Ctrl+Z == 26
        return;
    }
    if (ctrl) {
        switch (key) {
            case SDLK_LEFTBRACKET:  push_key(27); return; // Ctrl+[
            case SDLK_SLASH:        push_key(31); return; // Ctrl+/ (gedi toggle comment)
            case SDLK_BACKSLASH:    push_key(28); return;
            case SDLK_RIGHTBRACKET: push_key(29); return;
            default: break;
        }
    }
    // plain printable characters are delivered through SDL_TEXTINPUT
}

static void translate_event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_QUIT:
            push_alt('x');   // route through the normal Alt+X exit path
            return;
        case SDL_WINDOWEVENT:
            // Only react to genuine user/WM resizes (RESIZED).  SDL_WINDOWEVENT_
            // SIZE_CHANGED also fires for programmatic SDL_SetWindowSize calls made
            // by handleResize(), which would create an endless resize feedback loop.
            if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                g_eng.handleResize(e.window.data1, e.window.data2);
                resize_to_engine();
                push_key(KEY_RESIZE);
            } else if (e.window.event == SDL_WINDOWEVENT_LEAVE) {
                SDL_ShowCursor(SDL_ENABLE);
                g_mouse_inside = false;
                g_eng.updateMousePos(-1, -1, false);
            } else if (e.window.event == SDL_WINDOWEVENT_ENTER) {
                SDL_ShowCursor(SDL_DISABLE);
                g_mouse_inside = true;
            }
            return;
        case SDL_MOUSEMOTION: {
            int gx, gy;
            g_eng.pixelToGrid(e.motion.x, e.motion.y, &gx, &gy);
            g_eng.updateMousePos(gx, gy, true);
            if (g_btn_down) {
                push_mouse(gx, gy, BUTTON1_MOTION);
            } else if (gx != g_last_motion_gx || gy != g_last_motion_gy) {
                // Plain hover (no button): emit a position report, mirroring the
                // terminal's 1003 any-motion tracking, so menus get hover-highlight
                // and hover-to-switch.  Throttled to one event per grid cell so we
                // don't flood the input queue with sub-cell pixel moves.
                push_mouse(gx, gy, REPORT_MOUSE_POSITION);
            }
            g_last_motion_gx = gx; g_last_motion_gy = gy;
            return;
        }
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                int gx, gy;
                g_eng.pixelToGrid(e.button.x, e.button.y, &gx, &gy);
                g_btn_down = true;
                push_mouse(gx, gy, e.button.clicks >= 2 ? BUTTON1_DOUBLE_CLICKED
                                                        : BUTTON1_PRESSED);
            }
            return;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                int gx, gy;
                g_eng.pixelToGrid(e.button.x, e.button.y, &gx, &gy);
                g_btn_down = false;
                push_mouse(gx, gy, BUTTON1_RELEASED);
            }
            return;
        case SDL_MOUSEWHEEL: {
            int mx, my, gx, gy;
            SDL_GetMouseState(&mx, &my);
            g_eng.pixelToGrid(mx, my, &gx, &gy);
            push_mouse(gx, gy, e.wheel.y > 0 ? BUTTON4_PRESSED : BUTTON5_PRESSED);
            return;
        }
        case SDL_KEYDOWN:
            translate_keydown(e.key.keysym.sym, e.key.keysym.mod);
            return;
        case SDL_TEXTINPUT: {
            Uint16 mods = SDL_GetModState();
            bool alt   = (mods & KMOD_ALT) != 0;
            bool altgr = (mods & KMOD_RALT) != 0;
            bool ctrl  = (mods & KMOD_CTRL) != 0;
            if ((alt && !altgr) || ctrl) return;     // handled in KEYDOWN
            // decode UTF-8 -> code points
            const unsigned char* s = (const unsigned char*)e.text.text;
            while (*s) {
                wint_t cp; int n;
                if (*s < 0x80)        { cp = *s; n = 1; }
                else if (*s < 0xE0)   { cp = *s & 0x1F; n = 2; }
                else if (*s < 0xF0)   { cp = *s & 0x0F; n = 3; }
                else                  { cp = *s & 0x07; n = 4; }
                for (int i = 1; i < n && s[i]; ++i) cp = (cp << 6) | (s[i] & 0x3F);
                push_key(cp);
                s += n;
            }
            return;
        }
        default: return;
    }
}

static void pump_sdl() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) translate_event(e);
}

// ===========================================================================
//  Public ncurses-compatible API
// ===========================================================================

static std::string locate_font() {
    const char* candidates[] = {
        "VGA9.F16",
        "graphics_engine/VGA9.F16",
        "/usr/share/gedi/VGA9.F16",
        "/usr/local/share/gedi/VGA9.F16",
    };
    for (const char* c : candidates) {
        if (FILE* f = fopen(c, "rb")) { fclose(f); return c; }
    }
    // alongside the executable
    char* base = SDL_GetBasePath();
    if (base) {
        std::string p = std::string(base) + "VGA9.F16";
        SDL_free(base);
        if (FILE* f = fopen(p.c_str(), "rb")) { fclose(f); return p; }
    }
    return "VGA9.F16";
}

WINDOW* initscr() {
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    g_eng.font_path = locate_font();
    g_eng.init_sdl();
    SDL_StartTextInput();
    for (int i = 0; i < 256; ++i) { g_pair_fg[i] = COLOR_WHITE; g_pair_bg[i] = COLOR_BLACK; }
    stdscr = new CursesWindow();
    win_resize(stdscr, g_eng.SCREEN_ROWS, g_eng.SCREEN_COLS, 0);
    COLS  = g_eng.SCREEN_COLS;
    LINES = g_eng.SCREEN_ROWS;
    return stdscr;
}

int endwin() {
    SDL_StopTextInput();
    g_eng.shutdown();
    return OK;
}

int cbreak()                 { return OK; }
int noecho()                 { return OK; }
int raw()                    { return OK; }
int keypad(WINDOW*, bool)    { return OK; }
int notimeout(WINDOW*, bool) { return OK; }
int nodelay(WINDOW* w, bool bf) { if (w == stdscr || !w) g_nodelay = bf; return OK; }
void timeout(int delay)      { g_timeout = delay; }
void wtimeout(WINDOW*, int d) { g_timeout = d; }
int start_color()            { return OK; }
int use_default_colors()     { return OK; }
int set_escdelay(int)        { return OK; }
int def_prog_mode()          { return OK; }
int reset_prog_mode()        { return OK; }
int flushinp()               { g_input.clear(); g_mouse.clear(); return OK; }
int beep()                   { return OK; }
int flash()                  { return OK; }
bool has_colors()            { return true; }

int napms(int ms) {
    Uint32 end = SDL_GetTicks() + (Uint32)ms;
    do { pump_sdl(); blit_and_present(); SDL_Delay(4); }
    while (SDL_GetTicks() < end);
    return OK;
}

int curs_set(int v) {
    int prev = g_cursor_visible ? 1 : 0;
    g_cursor_visible = (v != 0);
    return prev;
}

int init_pair(short pair, short f, short b) {
    if (pair >= 0 && pair < 256) { g_pair_fg[pair] = f; g_pair_bg[pair] = b; }
    return OK;
}
int pair_content(short pair, short* f, short* b) {
    if (pair >= 0 && pair < 256) { if (f) *f = g_pair_fg[pair]; if (b) *b = g_pair_bg[pair]; }
    return OK;
}

mmask_t mousemask(mmask_t newmask, mmask_t* oldmask) { if (oldmask) *oldmask = 0; return newmask; }
int     mouseinterval(int)  { return 0; }
int     getmouse(MEVENT* ev) {
    if (g_mouse.empty()) return ERR;
    if (ev) *ev = g_mouse.front();
    g_mouse.pop_front();
    return OK;
}

char* tigetstr(const char*)            { return nullptr; }
int   define_key(const char*, int)     { return OK; }

// ---- attributes -----------------------------------------------------------
int wattron(WINDOW* w, int a)  { if (w) w->cur_attr |= (chtype)a;  return OK; }
int wattroff(WINDOW* w, int a) { if (w) w->cur_attr &= ~(chtype)a; return OK; }
int wattrset(WINDOW* w, int a) { if (w) w->cur_attr = (chtype)a;   return OK; }
int attron(int a)              { return wattron(stdscr, a); }
int attroff(int a)             { return wattroff(stdscr, a); }
int attrset(int a)             { return wattrset(stdscr, a); }

// ---- movement -------------------------------------------------------------
int wmove(WINDOW* w, int y, int x) { if (w) { w->cury = y; w->curx = x; } return OK; }
int move(int y, int x)             { return wmove(stdscr, y, x); }

// ---- character output -----------------------------------------------------
// addch merges any attribute/colour bits encoded in the chtype argument with
// the window's current attribute (this is how ncurses behaves).
int waddch(WINDOW* w, const chtype ch) {
    if (!w) return ERR;
    chtype extra = ch & ~A_CHARTEXT;
    wchar_t glyph = (wchar_t)(ch & A_CHARTEXT);
    if ((ch & ~A_CHARTEXT) != 0 && (ch & A_CHARTEXT) == 0) {
        // pure attribute/ACS code points stored as full chtype (e.g. ACS_*)
        glyph = (wchar_t)ch;
        extra = 0;
    }
    // ACS_* and similar are passed as raw code points >= 0x80 with no attr bits
    if (ch >= 0x80) { glyph = (wchar_t)ch; extra = 0; }
    win_put(w, w->cury, w->curx, glyph, w->cur_attr | extra);
    if (w->curx < w->cols - 1) w->curx++;
    return OK;
}
int addch(const chtype ch)                       { return waddch(stdscr, ch); }
int mvwaddch(WINDOW* w, int y, int x, const chtype ch) { wmove(w, y, x); return waddch(w, ch); }
int mvaddch(int y, int x, const chtype ch)       { wmove(stdscr, y, x); return waddch(stdscr, ch); }

// addstr decodes UTF-8 and writes one cell per code point, clipped to the row.
int waddstr(WINDOW* w, const char* str) {
    if (!w || !str) return ERR;
    const unsigned char* s = (const unsigned char*)str;
    while (*s) {
        wint_t cp; int n;
        if (*s < 0x80)      { cp = *s; n = 1; }
        else if (*s < 0xE0) { cp = *s & 0x1F; n = 2; }
        else if (*s < 0xF0) { cp = *s & 0x0F; n = 3; }
        else                { cp = *s & 0x07; n = 4; }
        for (int i = 1; i < n; ++i) { if (!s[i]) { n = i; break; } cp = (cp << 6) | (s[i] & 0x3F); }
        if (w->curx >= 0 && w->curx < w->cols)
            win_put(w, w->cury, w->curx, (wchar_t)cp, w->cur_attr);
        w->curx++;
        s += n;
    }
    return OK;
}
int addstr(const char* str)                         { return waddstr(stdscr, str); }
int mvwaddstr(WINDOW* w, int y, int x, const char* s) { wmove(w, y, x); return waddstr(w, s); }
int mvaddstr(int y, int x, const char* s)           { wmove(stdscr, y, x); return waddstr(stdscr, s); }

static int vwprintw_impl(WINDOW* w, const char* fmt, va_list ap) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    return waddstr(w, buf);
}
int printw(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = vwprintw_impl(stdscr, fmt, ap); va_end(ap); return r;
}
int mvprintw(int y, int x, const char* fmt, ...) {
    wmove(stdscr, y, x);
    va_list ap; va_start(ap, fmt); int r = vwprintw_impl(stdscr, fmt, ap); va_end(ap); return r;
}
int mvwprintw(WINDOW* w, int y, int x, const char* fmt, ...) {
    wmove(w, y, x);
    va_list ap; va_start(ap, fmt); int r = vwprintw_impl(w, fmt, ap); va_end(ap); return r;
}

// ---- lines ----------------------------------------------------------------
int mvwhline(WINDOW* w, int y, int x, chtype ch, int n) {
    wchar_t g = (ch >= 0x80) ? (wchar_t)ch : (wchar_t)(ch & A_CHARTEXT);
    if (g == 0) g = uni_to_cp437(L'─');
    for (int i = 0; i < n; ++i) win_put(w, y, x + i, g, w->cur_attr);
    return OK;
}
int mvwvline(WINDOW* w, int y, int x, chtype ch, int n) {
    wchar_t g = (ch >= 0x80) ? (wchar_t)ch : (wchar_t)(ch & A_CHARTEXT);
    if (g == 0) g = uni_to_cp437(L'│');
    for (int i = 0; i < n; ++i) win_put(w, y + i, x, g, w->cur_attr);
    return OK;
}
int mvhline(int y, int x, chtype ch, int n) { return mvwhline(stdscr, y, x, ch, n); }
int mvvline(int y, int x, chtype ch, int n) { return mvwvline(stdscr, y, x, ch, n); }
int hline(chtype ch, int n) { return mvwhline(stdscr, stdscr->cury, stdscr->curx, ch, n); }
int vline(chtype ch, int n) { return mvwvline(stdscr, stdscr->cury, stdscr->curx, ch, n); }

// ---- wide characters ------------------------------------------------------
int setcchar(cchar_t* c, const wchar_t* wch, const attr_t attrs, short pair, const void*) {
    if (!c) return ERR;
    int i = 0;
    if (wch) { for (; wch[i] && i < 7; ++i) c->chars[i] = wch[i]; }
    c->chars[i] = 0;
    c->attr = attrs;
    c->ext_color = pair;
    return OK;
}
int getcchar(const cchar_t* c, wchar_t* wch, attr_t* attrs, short* pair, void*) {
    if (!c) return ERR;
    if (wch) { int i = 0; for (; c->chars[i] && i < 7; ++i) wch[i] = c->chars[i]; wch[i] = 0; }
    if (attrs) *attrs = c->attr;
    if (pair)  *pair  = c->ext_color;
    return OK;
}
int wadd_wch(WINDOW* w, const cchar_t* c) {
    if (!w || !c) return ERR;
    chtype attr = (w->cur_attr & ~A_COLOR);
    attr |= COLOR_PAIR(c->ext_color);
    attr |= (c->attr & (A_BOLD | A_UNDERLINE | A_REVERSE | A_DIM));
    win_put(w, w->cury, w->curx, c->chars[0] ? c->chars[0] : L' ', attr);
    if (w->curx < w->cols - 1) w->curx++;
    return OK;
}
int add_wch(const cchar_t* c)                      { return wadd_wch(stdscr, c); }
int mvwadd_wch(WINDOW* w, int y, int x, const cchar_t* c) { wmove(w, y, x); return wadd_wch(w, c); }
int mvadd_wch(int y, int x, const cchar_t* c)      { wmove(stdscr, y, x); return wadd_wch(stdscr, c); }

int mvwin_wch(WINDOW* w, int y, int x, cchar_t* c) {
    if (!c) return ERR;
    memset(c, 0, sizeof(*c));
    if (win_inside(w, y, x)) {
        const CursesCell& cell = win_cell(w, y, x);
        c->chars[0] = cell.ch;
        c->ext_color = (short)PAIR_NUMBER(cell.attr);
        c->attr = cell.attr & (A_BOLD | A_UNDERLINE | A_REVERSE | A_DIM);
    } else {
        c->chars[0] = L' ';
    }
    return OK;
}
int mvin_wch(int y, int x, cchar_t* c) { return mvwin_wch(stdscr, y, x, c); }

int mvwhline_set(WINDOW* w, int y, int x, const cchar_t* c, int n) {
    chtype attr = (w->cur_attr & ~A_COLOR) | COLOR_PAIR(c->ext_color);
    for (int i = 0; i < n; ++i) win_put(w, y, x + i, c->chars[0], attr);
    return OK;
}
int mvwvline_set(WINDOW* w, int y, int x, const cchar_t* c, int n) {
    chtype attr = (w->cur_attr & ~A_COLOR) | COLOR_PAIR(c->ext_color);
    for (int i = 0; i < n; ++i) win_put(w, y + i, x, c->chars[0], attr);
    return OK;
}
int mvhline_set(int y, int x, const cchar_t* c, int n) { return mvwhline_set(stdscr, y, x, c, n); }
int mvvline_set(int y, int x, const cchar_t* c, int n) { return mvwvline_set(stdscr, y, x, c, n); }

// ---- erase / refresh ------------------------------------------------------
int werase(WINDOW* w) {
    if (!w) return ERR;
    std::fill(w->cells.begin(), w->cells.end(), CursesCell{L' ', w->bkgd});
    w->cury = w->curx = 0;
    return OK;
}
int wclear(WINDOW* w)  { return werase(w); }
int clear()            { return werase(stdscr); }
int erase()            { return werase(stdscr); }
int wclrtoeol(WINDOW* w) {
    if (!w) return ERR;
    for (int x = w->curx; x < w->cols; ++x) win_put(w, w->cury, x, L' ', w->bkgd);
    return OK;
}
int clrtoeol()         { return wclrtoeol(stdscr); }

int wrefresh(WINDOW* w) {
    if (w && w != stdscr) {
        // overlay the window onto stdscr at its origin, then present
        for (int y = 0; y < w->rows; ++y)
            for (int x = 0; x < w->cols; ++x)
                if (win_inside(stdscr, w->begy + y, w->begx + x))
                    win_cell(stdscr, w->begy + y, w->begx + x) = win_cell(w, y, x);
    }
    blit_and_present();
    return OK;
}
int refresh()                 { return wrefresh(stdscr); }
int wnoutrefresh(WINDOW* w)   { return wrefresh(w); }
int doupdate()                { blit_and_present(); return OK; }
int clearok(WINDOW*, bool)    { return OK; }
int touchwin(WINDOW*)         { return OK; }

int wbkgd(WINDOW* w, chtype ch) {
    if (!w) return ERR;
    w->bkgd = ch;
    w->cur_attr = ch;                       // subsequent writes adopt the background
    for (auto& cell : w->cells) { cell.ch = L' '; cell.attr = ch; }
    return OK;
}

// ---- windows --------------------------------------------------------------
WINDOW* newwin(int nlines, int ncols, int begin_y, int begin_x) {
    WINDOW* w = new CursesWindow();
    w->begy = begin_y; w->begx = begin_x;
    win_resize(w, nlines, ncols, 0);
    return w;
}
int delwin(WINDOW* w) { if (w && w != stdscr) delete w; return OK; }

int copywin(const WINDOW* src, WINDOW* dst, int sminrow, int smincol,
            int dminrow, int dmincol, int dmaxrow, int dmaxcol, int /*overlay*/) {
    if (!src || !dst) return ERR;
    for (int dy = dminrow; dy <= dmaxrow; ++dy) {
        for (int dx = dmincol; dx <= dmaxcol; ++dx) {
            int sy = sminrow + (dy - dminrow);
            int sx = smincol + (dx - dmincol);
            if (!win_inside((WINDOW*)src, sy, sx)) continue;
            if (!win_inside(dst, dy, dx)) continue;
            win_cell(dst, dy, dx) = win_cell((WINDOW*)src, sy, sx);
        }
    }
    return OK;
}

int box(WINDOW* w, chtype verch, chtype horch) {
    if (!w || w->rows < 2 || w->cols < 2) return ERR;
    wchar_t v = verch ? (wchar_t)verch : (wchar_t)0x2502;
    wchar_t h = horch ? (wchar_t)horch : (wchar_t)0x2500;
    int R = w->rows, C = w->cols;
    chtype a = w->cur_attr;
    win_put(w, 0, 0, 0x250C, a); win_put(w, 0, C - 1, 0x2510, a);
    win_put(w, R - 1, 0, 0x2514, a); win_put(w, R - 1, C - 1, 0x2518, a);
    for (int x = 1; x < C - 1; ++x) { win_put(w, 0, x, h, a); win_put(w, R - 1, x, h, a); }
    for (int y = 1; y < R - 1; ++y) { win_put(w, y, 0, v, a); win_put(w, y, C - 1, v, a); }
    return OK;
}

// ---- input ----------------------------------------------------------------
int wget_wch(WINDOW*, wint_t* wch) {
    for (;;) {
        pump_sdl();
        if (!g_input.empty()) {
            wint_t k = g_input.front();
            g_input.pop_front();
            if (wch) *wch = k;
            return (k >= KEY_MIN) ? KEY_CODE_YES : OK;
        }
        // queue empty
        bool blocking = !g_nodelay && g_timeout != 0;
        if (!blocking) {
            SDL_Delay(3);            // cap CPU while the editor idle-polls
            return ERR;
        }
        // blocking / timed wait: keep the UI alive.  Re-present on EVERY iteration
        // (not just once up-front) so the text-mode mouse overlay keeps tracking
        // pointer motion and the cursor keeps blinking while a modal menu/dialog is
        // blocked on a key.  Plain pointer motion enqueues no input event, so
        // without re-presenting here the screen would freeze until the next
        // keystroke or click — which is why the red mouse cursor appeared stuck
        // whenever a menu was open.
        int waited = 0;
        while (g_input.empty()) {
            blit_and_present();
            SDL_Delay(8);
            pump_sdl();
            waited += 8;
            if (g_timeout > 0 && waited >= g_timeout) return ERR;
        }
    }
}
int get_wch(wint_t* wch)       { return wget_wch(stdscr, wch); }
int wgetch(WINDOW* w)          { wint_t c; return (wget_wch(w, &c) == ERR) ? ERR : (int)c; }
int getch()                    { return wgetch(stdscr); }
int unget_wch(const wchar_t c) { g_input.push_front((wint_t)c); return OK; }
int ungetch(int c)             { g_input.push_front((wint_t)c); return OK; }

#endif // GEDI_GUI
