#pragma once

void gotoxy(int x, int y);
void textcolor(int c);
void textbackground(int c);
void window(int x1, int y1, int x2, int y2);
void clrscr();
void cprintf(const char* fmt, ...);
void line(int n, char c);
void boxi(int a, int b, int c, int d, int p1 = 0, int p2 = 0);
void drawFrame(int h, int w, int type);
void drawMenuBox(int x, int y, int w, int h, int p1=0, int p2=0);
void windowBorder(int a, int b, int c, int d);
void drawMenuItem(int itemIndex, int x, int y, int width, const char* text, const char* rightText, char redChar = '\0');
void drawButton(int x, int y, const char* text, bool active, char hotkey, bool isDefault=false);