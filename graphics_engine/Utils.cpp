#include "Utils.h"
#include "GlobalState.h"
#include "BorlandEngine.h"
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>

// Wrapper functions
void gotoxy(int x, int y) { eng.gotoxy(x, y); }
void textcolor(int c) { eng.textcolor(c); }
void textbackground(int c) { eng.textbackground(c); }
void window(int x1, int y1, int x2, int y2) { eng.set_window(x1, y1, x2, y2); }
void clrscr() { eng.clrscr(); }
void cprintf(const char* fmt, ...) {
    char buf[1024]; va_list args; va_start(args, fmt); vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
    eng.cprintf("%s", buf);
}
void line(int n, char c) { for (int i = 0; i < n; i++) cprintf("%c", (unsigned char)c); }

void boxi(int a, int b, int c, int d, int p1, int p2) {
    textcolor(BLACK);
    gotoxy(a, b); line(d, (char)196);
    gotoxy(a, c); line(d, (char)196);
    for (int i = b + 1; i < c; i++) {
        gotoxy(a, i); cprintf("%c", (char)179); gotoxy(a + d - 1, i); cprintf("%c", (char)179);
    }
    gotoxy(a, b); cprintf("%c", (char)218); gotoxy(a, c); cprintf("%c", (char)192);
    gotoxy(a + d - 1, b); cprintf("%c", (char)191); gotoxy(a + d - 1, c); cprintf("%c", (char)217);
    if (p1) { gotoxy(a, b + p1); cprintf("%c", (char)195); line(d - 2, (char)196); cprintf("%c", (char)180); }
    if (p2) { gotoxy(a, b + p2); cprintf("%c", (char)195); line(d - 2, (char)196); cprintf("%c", (char)180); }
}

void drawFrame(int h, int w, int type) {
    unsigned char tl, tr, bl, br, hor, ver;
    if (type == 1) { tl = 201; tr = 187; bl = 200; br = 188; hor = 205; ver = 186; } 
    else { tl = 218; tr = 191; bl = 192; br = 217; hor = 196; ver = 179; }
    gotoxy(1, 1); cprintf("%c", tl); line(w - 2, hor); cprintf("%c", tr);
    for (int i = 2; i < h; i++) {
        gotoxy(1, i); cprintf("%c", ver); gotoxy(w, i); cprintf("%c", ver);
    }
    gotoxy(1, h); cprintf("%c", bl); line(w - 2, hor); cprintf("%c", br);
}

void drawMenuBox(int x, int y, int w, int h, int p1, int p2) {
    eng.make_shadow(x + 2, y + 1, w, h);
    window(x, y, x + w - 1, y + h - 1);
    textbackground(WHITE); clrscr();
    boxi(1, 1, h, w, p1, p2);
}

void drawMenuItem(int itemIndex, int x, int y, int width, const char* text, const char* rightText, char redChar) {
    if (itemIndex == currentItemIndex) { textbackground(GREEN); textcolor(BLACK); } 
    else { textbackground(WHITE); textcolor(BLACK); }
    gotoxy(x, y);
    int padding = width - strlen(text) - strlen(rightText) - 2; 
    if (padding < 0) padding = 0;
    cprintf(" %s", text);
    for(int i=0; i<padding; i++) cprintf(" ");
    cprintf("%s ", rightText);
    if (redChar != '\0') {
        std::string s = text; size_t pos = s.find(redChar);
        if (pos != std::string::npos) {
            gotoxy(x + 1 + pos, y);
            textcolor(RED); 
            if (itemIndex == currentItemIndex) textbackground(GREEN); else textbackground(WHITE);
            cprintf("%c", redChar);
        }
    }
}

void drawButton(int x, int y, const char* text, bool active, char hotkey, bool isDefault) {
    int w = strlen(text) + 4;
    window(1, 1, eng.SCREEN_COLS, eng.SCREEN_ROWS); 
    for(int i=0; i<w; i++) {
        eng.put_raw(x + i, y, ' '); 
        int idx = (y-1)*eng.SCREEN_COLS + (x+i-1);
        if (idx >= 0 && idx < eng.buffer.size()) eng.buffer[idx].bg_index = GREEN;
    }
    int textX = x + 2;
    for(int i=0; i<strlen(text); i++) {
        eng.put_raw(textX + i, y, text[i]);
        int idx = (y-1)*eng.SCREEN_COLS + (textX+i-1);
        if (idx >= 0 && idx < eng.buffer.size()) {
            if (active) eng.buffer[idx].fg_index = WHITE;
            else if (isDefault) eng.buffer[idx].fg_index = YELLOW;
            else eng.buffer[idx].fg_index = BLACK;
            eng.buffer[idx].bg_index = GREEN;
            if (text[i] == hotkey) {
                if (active) eng.buffer[idx].fg_index = WHITE;
                else if (isDefault) eng.buffer[idx].fg_index = YELLOW;
                else eng.buffer[idx].fg_index = RED;
            }
        }
    }
    eng.put_raw(x + w, y, 221);
    eng.put_raw(x + w, y + 1, 223);
    for(int i=1; i<=w; i++) eng.put_raw(x + i, y + 1, 223);
}