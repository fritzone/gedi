// curses_compat.h
//
// Single include point that replaces every `#include <ncurses.h>` in the gedi
// sources.  When the project is built as the text-mode application this header
// is completely transparent and just pulls in the real ncurses library.  When
// the project is built with -DGEDI_GUI it instead provides a small ncurses
// emulation layer that renders the very same character grid through the SDL
// based BorlandEngine (a VGA text-mode emulator).  The rest of the code base is
// oblivious to which backend it is talking to.
#ifndef GEDI_CURSES_COMPAT_H
#define GEDI_CURSES_COMPAT_H

#ifndef GEDI_GUI
// ---------------------------------------------------------------------------
//  Text-mode build: just use the real thing.
// ---------------------------------------------------------------------------
#include <ncurses.h>

#else
// ---------------------------------------------------------------------------
//  Graphical build: emulate the slice of the ncurses API that gedi relies on.
// ---------------------------------------------------------------------------
#include <cwchar>
#include <cstddef>
#include <vector>

// ---- basic types ----------------------------------------------------------
typedef unsigned int  chtype;   // character + attribute cell value
typedef unsigned int  attr_t;
typedef unsigned long mmask_t;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define ERR (-1)
#define OK  (0)

// A single character cell of a window.
struct CursesCell {
    wchar_t ch;     // unicode code point stored in the cell (' ' when empty)
    chtype  attr;   // colour-pair number (bits 8-15) + A_* attribute bits
};

// A window is just an addressable grid of cells plus a little cursor/attr state.
struct CursesWindow {
    int   begy = 0, begx = 0;       // origin on the physical screen
    int   rows = 0, cols = 0;       // size
    int   cury = 0, curx = 0;       // logical cursor position
    chtype cur_attr = 0;            // attribute applied to the next write
    chtype bkgd     = 0;            // background fill attribute
    std::vector<CursesCell> cells;  // rows*cols cells, row-major
};
typedef CursesWindow WINDOW;

// Complex (wide) character used by the *_wch family.
typedef struct {
    wchar_t chars[8];
    attr_t  attr;
    short   ext_color;   // colour-pair number
} cchar_t;

// Mouse event, mirrors the ncurses MEVENT layout used by gedi.
typedef struct {
    short   id;
    int     x, y, z;
    mmask_t bstate;
} MEVENT;

// ---- attribute / colour bit layout ---------------------------------------
// COLOR_PAIR(n) keeps the pair number in bits 8-15, exactly like ncurses, so
// code that ORs colour pairs into a chtype keeps working.
#define A_NORMAL    ((attr_t)0)
#define WA_NORMAL   A_NORMAL
#define A_CHARTEXT  ((attr_t)0x000000ff)
#define A_COLOR     ((attr_t)0x0000ff00)
#define COLOR_PAIR(n) ((chtype)(((unsigned)(n) & 0xff) << 8))
#define PAIR_NUMBER(a) (((unsigned)(a) >> 8) & 0xff)
#define A_UNDERLINE ((attr_t)(1u << 17))
#define A_REVERSE   ((attr_t)(1u << 18))
#define A_BLINK     ((attr_t)(1u << 19))
#define A_DIM       ((attr_t)(1u << 20))
#define A_BOLD      ((attr_t)(1u << 21))

// ---- colours (ncurses numbering) -----------------------------------------
#define COLOR_BLACK   0
#define COLOR_RED     1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_BLUE    4
#define COLOR_MAGENTA 5
#define COLOR_CYAN    6
#define COLOR_WHITE   7

// ---- alternate character set (mapped to CP437 code points) ----------------
// These are the glyphs the code draws via mvaddch(); the emulator turns the
// code point into the matching CP437 byte of the VGA font.
#define ACS_ULCORNER ((chtype)0x250C)
#define ACS_URCORNER ((chtype)0x2510)
#define ACS_LLCORNER ((chtype)0x2514)
#define ACS_LRCORNER ((chtype)0x2518)
#define ACS_LTEE     ((chtype)0x251C)
#define ACS_RTEE     ((chtype)0x2524)
#define ACS_TTEE     ((chtype)0x252C)
#define ACS_BTEE     ((chtype)0x2534)
#define ACS_HLINE    ((chtype)0x2500)
#define ACS_VLINE    ((chtype)0x2502)
#define ACS_PLUS     ((chtype)0x253C)
#define ACS_BLOCK    ((chtype)0x2588)
#define ACS_CKBOARD  ((chtype)0x2592)
#define ACS_BOARD    ((chtype)0x2591)
#define ACS_DEGREE   ((chtype)0x00B0)
#define ACS_BULLET   ((chtype)0x00B7)
#define ACS_LARROW   ((chtype)0x2190)
#define ACS_RARROW   ((chtype)0x2192)
#define ACS_UARROW   ((chtype)0x2191)
#define ACS_DARROW   ((chtype)0x2193)

// ---- key codes (identical numeric values to ncurses) ----------------------
#define KEY_CODE_YES 0400
#define KEY_MIN      0401
#define KEY_DOWN     0402
#define KEY_UP       0403
#define KEY_LEFT     0404
#define KEY_RIGHT    0405
#define KEY_HOME     0406
#define KEY_BACKSPACE 0407
#define KEY_F0       0410
#define KEY_F(n)     (KEY_F0 + (n))
#define KEY_DC       0512
#define KEY_IC       0513
#define KEY_SF       0520
#define KEY_SR       0521
#define KEY_NPAGE    0522
#define KEY_PPAGE    0523
#define KEY_ENTER    0527
#define KEY_BTAB     0541
#define KEY_END      0550
#define KEY_SDC      0577
#define KEY_SEND     0602
#define KEY_SHOME    0607
#define KEY_SIC      0610
#define KEY_SLEFT    0611
#define KEY_SNEXT    0614
#define KEY_SPREVIOUS 0616
#define KEY_SRIGHT   0622
#define KEY_MOUSE    0631
#define KEY_RESIZE   0632
#define KEY_MAX      0777

// ---- mouse state bits ------------------------------------------------------
#define BUTTON1_RELEASED       0x00000001UL
#define BUTTON1_PRESSED        0x00000002UL
#define BUTTON1_CLICKED        0x00000004UL
#define BUTTON1_DOUBLE_CLICKED 0x00000008UL
#define BUTTON1_TRIPLE_CLICKED 0x00000010UL
#define BUTTON1_MOTION         0x00000020UL
#define BUTTON2_PRESSED        0x00000040UL
#define BUTTON3_PRESSED        0x00000080UL
#define BUTTON4_PRESSED        0x00000100UL
#define BUTTON5_PRESSED        0x00000200UL
#define BUTTON_SHIFT           0x04000000UL
#define BUTTON_CTRL            0x08000000UL
#define BUTTON_ALT             0x10000000UL
#define REPORT_MOUSE_POSITION  0x20000000UL
#define ALL_MOUSE_EVENTS       0x1fffffffUL

// ---- globals ---------------------------------------------------------------
extern WINDOW* stdscr;
extern int     COLS;
extern int     LINES;

// getmaxyx / getyx are statement macros, just like in ncurses.
#define getmaxyx(win, Y, X) (void)((Y) = (win)->rows, (X) = (win)->cols)
#define getyx(win, Y, X)    (void)((Y) = (win)->cury, (X) = (win)->curx)
#define getbegyx(win, Y, X) (void)((Y) = (win)->begy, (X) = (win)->begx)

// ---- lifecycle / modes -----------------------------------------------------
WINDOW* initscr();
int     endwin();
int     cbreak();
int     noecho();
int     raw();
int     keypad(WINDOW* win, bool bf);
int     nodelay(WINDOW* win, bool bf);
int     notimeout(WINDOW* win, bool bf);
void    timeout(int delay);
void    wtimeout(WINDOW* win, int delay);
int     curs_set(int visibility);
int     start_color();
int     use_default_colors();
int     set_escdelay(int ms);
int     def_prog_mode();
int     reset_prog_mode();
int     flushinp();
int     beep();
int     flash();
int     napms(int ms);

// ---- colour ----------------------------------------------------------------
bool    has_colors();
int     init_pair(short pair, short f, short b);
int     pair_content(short pair, short* f, short* b);

// ---- mouse -----------------------------------------------------------------
mmask_t mousemask(mmask_t newmask, mmask_t* oldmask);
int     mouseinterval(int erval);
int     getmouse(MEVENT* event);

// ---- terminfo stubs --------------------------------------------------------
char*   tigetstr(const char* capname);
int     define_key(const char* definition, int keycode);

// ---- attributes ------------------------------------------------------------
int     attron(int attrs);
int     attroff(int attrs);
int     attrset(int attrs);
int     wattron(WINDOW* win, int attrs);
int     wattroff(WINDOW* win, int attrs);
int     wattrset(WINDOW* win, int attrs);

// ---- cursor movement -------------------------------------------------------
int     move(int y, int x);
int     wmove(WINDOW* win, int y, int x);

// ---- character / string output --------------------------------------------
int     addch(const chtype ch);
int     waddch(WINDOW* win, const chtype ch);
int     mvaddch(int y, int x, const chtype ch);
int     mvwaddch(WINDOW* win, int y, int x, const chtype ch);
int     addstr(const char* str);
int     waddstr(WINDOW* win, const char* str);
int     mvaddstr(int y, int x, const char* str);
int     mvwaddstr(WINDOW* win, int y, int x, const char* str);
int     printw(const char* fmt, ...);
int     mvprintw(int y, int x, const char* fmt, ...);
int     mvwprintw(WINDOW* win, int y, int x, const char* fmt, ...);

// ---- lines -----------------------------------------------------------------
int     hline(chtype ch, int n);
int     vline(chtype ch, int n);
int     mvhline(int y, int x, chtype ch, int n);
int     mvvline(int y, int x, chtype ch, int n);
int     mvwhline(WINDOW* win, int y, int x, chtype ch, int n);
int     mvwvline(WINDOW* win, int y, int x, chtype ch, int n);

// ---- wide character output -------------------------------------------------
int     setcchar(cchar_t* wcval, const wchar_t* wch, const attr_t attrs,
                 short color_pair, const void* opts);
int     getcchar(const cchar_t* wcval, wchar_t* wch, attr_t* attrs,
                 short* color_pair, void* opts);
int     add_wch(const cchar_t* wch);
int     mvadd_wch(int y, int x, const cchar_t* wch);
int     wadd_wch(WINDOW* win, const cchar_t* wch);
int     mvwadd_wch(WINDOW* win, int y, int x, const cchar_t* wch);
int     mvin_wch(int y, int x, cchar_t* wcval);
int     mvwin_wch(WINDOW* win, int y, int x, cchar_t* wcval);
int     mvwhline_set(WINDOW* win, int y, int x, const cchar_t* wch, int n);
int     mvwvline_set(WINDOW* win, int y, int x, const cchar_t* wch, int n);
int     mvhline_set(int y, int x, const cchar_t* wch, int n);
int     mvvline_set(int y, int x, const cchar_t* wch, int n);

// ---- erase / refresh -------------------------------------------------------
int     clear();
int     erase();
int     wclear(WINDOW* win);
int     werase(WINDOW* win);
int     clrtoeol();
int     wclrtoeol(WINDOW* win);
int     refresh();
int     wrefresh(WINDOW* win);
int     wnoutrefresh(WINDOW* win);
int     doupdate();
int     clearok(WINDOW* win, bool bf);
int     touchwin(WINDOW* win);
int     wbkgd(WINDOW* win, chtype ch);

// ---- windows ---------------------------------------------------------------
WINDOW* newwin(int nlines, int ncols, int begin_y, int begin_x);
int     delwin(WINDOW* win);
int     copywin(const WINDOW* srcwin, WINDOW* dstwin, int sminrow, int smincol,
                int dminrow, int dmincol, int dmaxrow, int dmaxcol, int overlay);
int     box(WINDOW* win, chtype verch, chtype horch);

// ---- input -----------------------------------------------------------------
int     getch();
int     wgetch(WINDOW* win);
int     get_wch(wint_t* wch);
int     wget_wch(WINDOW* win, wint_t* wch);
int     ungetch(int ch);
int     unget_wch(const wchar_t wch);

#endif // GEDI_GUI
#endif // GEDI_CURSES_COMPAT_H
