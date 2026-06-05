#include "UI.h"
#include "BorlandEngine.h"
#include "WindowSystem.h"
#include "Utils.h"
#include "GlobalState.h"
#include "Constants.h"
#include <cstdio>
#include <algorithm>

int getMenuSize(int menuIdx) {
    switch(menuIdx) {
    case M_SYSTEM: return 2;
    case M_FILE: return 9;
    case M_EDIT: return 8;
    case M_SEARCH: return 7;
    case M_RUN: return 6;
    case M_COMPILE: return 6;
    case M_DEBUG: return 6;
    case M_PROJECT: return 6;
    case M_OPTIONS: return 8;
    case M_WINDOW: return 8;
    case M_HELP: return 6;
    default: return 0;
    }
}

std::string getHelpText(int menu, int item) {
    if (wm.fileDialog->active) return "Enter directory or filename";
    if (isResizing) return "Arrow keys to move, Shift+Arrow keys to resize, Enter to confirm, Esc to cancel";
    if (menu == -1) return "F1-Help | View help on using the environment";

    if (menu == M_SYSTEM) {
        if (item == 0) return "Toggle between retro (pixelated) and smooth graphics";
        if (item == 1) return "Toggle whether the application window can be resized";
    }

    return "";
}


void processMenuAction() {
    if (currentMenuIndex == -1) return;
    bool close = false;

    if (currentMenuIndex == M_SYSTEM) {
        if (currentItemIndex == 0) {
            eng.toggleSmoothScaling();
        } else if (currentItemIndex == 1) {
            eng.toggleResizable();
        } else {
            close = true;
        }
    } else if (currentMenuIndex == M_FILE) {
        if (currentItemIndex == 0) { newFileWindow(); close = true; }
        else if (currentItemIndex == 1) {
            // UPDATED: Now calls RequestOpen
            wm.requestOpen();
            close = true;
        }
        else if (currentItemIndex == 2) { wm.requestSave(false); close = true; }
        else if (currentItemIndex == 3) { wm.requestSave(true); close = true; }
        else if (currentItemIndex == 8) exit(0);
    } else if (currentMenuIndex == M_WINDOW) {
        auto win = wm.getActiveWindow();
        if (currentItemIndex == 0) { wm.startResize(); close = true; }
        else if (currentItemIndex == 1) { if(win) win->toggleZoom(); close = true; }
        else if (currentItemIndex == 4) wm.nextWindow();
        else if (currentItemIndex == 5) wm.closeActiveWindow();
        else if (currentItemIndex == 6) wm.closeAll();
        if (!close) close = true;
    } else {
        close = true;
    }

    if (close) {
        currentMenuIndex = -1;
        SDL_FlushEvents(SDL_TEXTINPUT, SDL_TEXTINPUT);
    }
}


void drawStatusBar() {
    window(1, 1, eng.SCREEN_COLS, eng.SCREEN_ROWS);
    int row = eng.SCREEN_ROWS;
    textbackground(WHITE); gotoxy(1, row); line(eng.SCREEN_COLS, ' ');
    gotoxy(2, row); textcolor(RED); cprintf("F1");
    textcolor(BLACK); cprintf(" - Help %c ", 179);
    cprintf("%s", getHelpText(currentMenuIndex, currentItemIndex).c_str());
}

void newFileWindow() {
    static int win_count = 0;
    int offset = win_count * 1; if (offset > 10) offset = 0;
    char title[32]; sprintf(title, "NONAME%02d.CPP", win_count);
    int w = std::min(60, eng.SCREEN_COLS - 10); int h = std::min(15, eng.SCREEN_ROWS - 5);
    auto win = std::make_shared<EditorWindow>(5 + offset, 5 + offset, w, h, title);
    wm.addWindow(win); win_count++;
}

void openFileWindow() {
    drawMenuBox(10, 8, 60, 10);
    gotoxy(5, 5); textcolor(WHITE); textbackground(BLUE); cprintf("Open File Window Stub");
}

void defaultMenu(void) {
    if(currentMenuIndex != M_SYSTEM) return;
    drawMenuBox(1, 2, 28, 4, 0);
    int w = 26;

    const char* smoothText = eng.smooth_scaling ? "Smooth graphics [ON]" : "Smooth graphics [OFF]";
    drawMenuItem(0, 2, 2, w, smoothText, "", 'g');

    const char* resText = eng.is_resizable ? "Window Resizable [ON]" : "Window Resizable [OFF]";
    drawMenuItem(1, 2, 3, w, resText, "", 'R');
}

void fileMenu(void) {
    if(currentMenuIndex != M_FILE) return;
    drawMenuBox(4, 2, 21, 13, 6, 10);
    int w = 19;
    drawMenuItem(0, 2, 2, w, "New", "", 'N');
    drawMenuItem(1, 2, 3, w, "Open...", "F3", 'O');
    drawMenuItem(2, 2, 4, w, "Save", "F2", 'S');
    drawMenuItem(3, 2, 5, w, "Save as...", "", 'a');
    drawMenuItem(4, 2, 6, w, "Save all", "", 'l');
    drawMenuItem(5, 2, 8, w, "Change dir...", "", 'C');
    drawMenuItem(6, 2, 9, w, "Print", "", 'P');
    drawMenuItem(7, 2, 10, w, "DOS shell", "", 'D');
    drawMenuItem(8, 2, 12, w, "Quit", "Alt+X", 'Q');
}

void editMenu(void) {
    if(currentMenuIndex != M_EDIT) return;
    drawMenuBox(10, 2, 30, 12, 3, 9);
    int w = 28;
    drawMenuItem(0, 2, 2, w, "Undo", "Alt+BkSp", 'U');
    drawMenuItem(1, 2, 3, w, "Redo", "Sft+Alt+BkSp", 'R');
    drawMenuItem(2, 2, 5, w, "Cut", "Shift+Del", 't');
    drawMenuItem(3, 2, 6, w, "Copy", "Ctrl+Ins", 'C');
    drawMenuItem(4, 2, 7, w, "Paste", "Shift+Ins", 'P');
    drawMenuItem(5, 2, 8, w, "Clear", "Ctrl+Del", 'l');
    drawMenuItem(6, 2, 9, w, "Copy example", "", 'e');
    drawMenuItem(7, 2, 11, w, "Show clipboard", "", 'S');
}

void searchMenu(void) {
    if(currentMenuIndex != M_SEARCH) return;
    drawMenuBox(16, 2, 31, 10, 4);
    int w = 29;
    drawMenuItem(0, 2, 2, w, "Find...", "", 'F');
    drawMenuItem(1, 2, 3, w, "Replace...", "", 'R');
    drawMenuItem(2, 2, 4, w, "Search again", "Ctrl+L", 'S');
    drawMenuItem(3, 2, 6, w, "Go to line num...", "", 'G');
    drawMenuItem(4, 2, 7, w, "Previous error", "Alt+F7", 'P');
    drawMenuItem(5, 2, 8, w, "Next error", "Alt+F8", 'N');
    drawMenuItem(6, 2, 9, w, "Locate function...", "", 'L');
}

void runMenu(void) {
    if(currentMenuIndex != M_RUN) return;
    drawMenuBox(24, 2, 29, 8);
    int w = 27;
    drawMenuItem(0, 2, 2, w, "Run", "Ctrl+F9", 'R');
    drawMenuItem(1, 2, 3, w, "Program reset", "Ctrl+F2", 'P');
    drawMenuItem(2, 2, 4, w, "Go to cursor", "F4", 'G');
    drawMenuItem(3, 2, 5, w, "Trace into", "F7", 'T');
    drawMenuItem(4, 2, 6, w, "Step over", "F8", 'S');
    drawMenuItem(5, 2, 7, w, "Arguments...", "", 'A');
}

void compileMenu(void) {
    if(currentMenuIndex != M_COMPILE) return;
    drawMenuBox(28, 2, 29, 9, 5);
    int w = 27;
    drawMenuItem(0, 2, 2, w, "Compile", "Alt+F9", 'C');
    drawMenuItem(1, 2, 3, w, "Make", "F9", 'M');
    drawMenuItem(2, 2, 4, w, "Link", "", 'L');
    drawMenuItem(3, 2, 5, w, "Build all", "", 'B');
    drawMenuItem(4, 2, 7, w, "Information...", "", 'I');
    drawMenuItem(5, 2, 8, w, "Remove messages", "", 'R');
}

void debugMenu(void) {
    if(currentMenuIndex != M_DEBUG) return;
    drawMenuBox(38, 2, 36, 9, 6);
    int w = 34;
    drawMenuItem(0, 2, 2, w, "Inspect...", "Alt+F4", 'I');
    drawMenuItem(1, 2, 3, w, "Evaluate/modify...", "Ctrl+F4", 'E');
    drawMenuItem(2, 2, 4, w, "Call stack", "Ctrl+F3", 'C');
    drawMenuItem(3, 2, 5, w, "Watches", "", 'W');
    drawMenuItem(4, 2, 6, w, "Toggle breakpoint", "Ctrl+F8", 'T');
    drawMenuItem(5, 2, 8, w, "Breakpoints...", "", 'B');
}

void projectMenu(void) {
    if(currentMenuIndex != M_PROJECT) return;
    drawMenuBox(46, 2, 26, 10, 3, 6);
    int w = 24;
    drawMenuItem(0, 2, 2, w, "Open project...", "", 'O');
    drawMenuItem(1, 2, 3, w, "Close project", "", 'C');
    drawMenuItem(2, 2, 5, w, "Add item...", "", 'A');
    drawMenuItem(3, 2, 6, w, "Delete item", "", 'D');
    drawMenuItem(4, 2, 8, w, "Local options...", "", 'L');
    drawMenuItem(5, 2, 9, w, "Include files...", "", 'I');
}

void optionsMenu(void) {
    if(currentMenuIndex != M_OPTIONS) return;
    drawMenuBox(55, 2, 24, 11, 7);
    int w = 22;
    drawMenuItem(0, 2, 2, w, "Application...", "", 'A');
    drawMenuItem(1, 2, 3, w, "Compiler...", "", 'C');
    drawMenuItem(2, 2, 4, w, "Make...", "", 'M');
    drawMenuItem(3, 2, 5, w, "Linker...", "", 'L');
    drawMenuItem(4, 2, 6, w, "Debugger...", "", 'D');
    drawMenuItem(5, 2, 7, w, "Directories...", "", 'i');
    drawMenuItem(6, 2, 9, w, "Environment...", "", 'E');
    drawMenuItem(7, 2, 10, w, "Save...", "", 'S');
}

void windowMenu(void) {
    if(currentMenuIndex != M_WINDOW) return;

    // Fix: Render logic matched to HandleMenuHover
    int menuX = eng.SCREEN_COLS - 25;
    if(menuX < 0) menuX = 0;

    drawMenuBox(menuX, 2, 24, 12, 3, 8);
    int w = 22;
    drawMenuItem(0, 2, 2, w, "Size/Move", "Ctrl+F5", 'S');
    drawMenuItem(1, 2, 3, w, "Zoom", "F5", 'Z');
    drawMenuItem(2, 2, 5, w, "Tile", "", 'T');
    drawMenuItem(3, 2, 6, w, "Cascade", "", 'C');
    drawMenuItem(4, 2, 7, w, "Next", "F6", 'N');
    drawMenuItem(5, 2, 8, w, "Close", "Alt+F3", 'l');
    drawMenuItem(6, 2, 10, w, "Close all", "", 'a');
    drawMenuItem(7, 2, 11, w, "List...", "Alt+0", 'L');
}

void helpMenu(void) {
    if(currentMenuIndex != M_HELP) return;
    int x = eng.SCREEN_COLS - 30; if(x < 0) x = 0;
    drawMenuBox(x, 2, 30, 9, 6);
    int w = 28;
    drawMenuItem(0, 2, 2, w, "Contents", "", 'C');
    drawMenuItem(1, 2, 3, w, "Index", "Shift+F1", 'I');
    drawMenuItem(2, 2, 4, w, "Topic search", "Ctrl+F1", 'T');
    drawMenuItem(3, 2, 5, w, "Previous topic", "Alt+F1", 'P');
    drawMenuItem(4, 2, 6, w, "Help on help", "", 'H');
    drawMenuItem(5, 2, 8, w, "About", "", 'A');
}

void MENU(int active) {
    window(1, 1, eng.SCREEN_COLS, eng.SCREEN_ROWS);
    textbackground(WHITE); textcolor(BLUE);
    for(int y=2; y<=eng.SCREEN_ROWS-1; y++) { gotoxy(1, y); for(int x=0; x<eng.SCREEN_COLS; x++) cprintf("%c", (char)176); }
    gotoxy(1, 1); line(eng.SCREEN_COLS, ' ');
    textcolor(BLACK);
    gotoxy(3, 1); cprintf("    ile   dit   earch   un   ompile   ebug   roject   ptions");
    std::string rightPart = "     indow   elp";
    int rightX = eng.SCREEN_COLS - rightPart.length() - 1;
    gotoxy(rightX, 1); cprintf("%s", rightPart.c_str());
    textcolor(RED);
    gotoxy(3, 1); cprintf("%c", (char)240); gotoxy(6, 1); cprintf("F"); gotoxy(12, 1); cprintf("E");
    gotoxy(18, 1); cprintf("S"); gotoxy(26, 1); cprintf("R"); gotoxy(31, 1); cprintf("C");
    gotoxy(40, 1); cprintf("D"); gotoxy(47, 1); cprintf("P"); gotoxy(56, 1); cprintf("O");
    gotoxy(rightX + 4, 1); cprintf("W"); gotoxy(rightX + 12, 1); cprintf("H");
    if (active) {
        textbackground(GREEN); textcolor(BLACK);
        switch(currentMenuIndex) {
        case M_SYSTEM: gotoxy(2, 1); cprintf(" %c ", (char)240); break;
        case M_FILE:   gotoxy(5, 1); cprintf(" File "); break;
        case M_EDIT:   gotoxy(11, 1); cprintf(" Edit "); break;
        case M_SEARCH: gotoxy(17, 1); cprintf(" Search "); break;
        case M_RUN:    gotoxy(25, 1); cprintf(" Run "); break;
        case M_COMPILE:gotoxy(30, 1); cprintf(" Compile "); break;
        case M_DEBUG:  gotoxy(39, 1); cprintf(" Debug "); break;
        case M_PROJECT:gotoxy(46, 1); cprintf(" Project "); break;
        case M_OPTIONS:gotoxy(55, 1); cprintf(" Options "); break;
        case M_WINDOW: gotoxy(rightX + 4, 1); cprintf(" Window "); break;
        case M_HELP:   gotoxy(rightX + 12, 1); cprintf(" Help "); break;
        }
    }
}


void checkMenuHotkey(SDL_Keycode key) {
    char k = (char)key;
    if (k >= 'a' && k <= 'z') k -= 32;
    int targetIndex = -1;
#define CHECK(idx, charCode) if (k == charCode) targetIndex = idx;

    if (currentMenuIndex == M_FILE) { CHECK(0, 'N'); CHECK(1, 'O'); CHECK(2, 'S'); CHECK(3, 'A'); CHECK(4, 'L'); CHECK(5, 'C'); CHECK(6, 'P'); CHECK(7, 'D'); CHECK(8, 'Q'); }
    else if (currentMenuIndex == M_WINDOW) { CHECK(0, 'S'); CHECK(1, 'Z'); CHECK(2, 'T'); CHECK(3, 'C'); CHECK(4, 'N'); CHECK(5, 'L'); CHECK(6, 'A'); CHECK(7, 'L'); }
    else if (currentMenuIndex == M_SYSTEM) { CHECK(0, 'G'); CHECK(1, 'R'); }

    if (targetIndex != -1) {
        currentItemIndex = targetIndex;
        processMenuAction();
    }
}

void HandleUIInput(SDL_Keycode key) {
    if (key == SDLK_ESCAPE) { currentMenuIndex = -1; }
    else if (key == SDLK_DOWN) {
        currentItemIndex++; if (currentItemIndex >= getMenuSize(currentMenuIndex)) currentItemIndex = 0;
    } else if (key == SDLK_UP) {
        currentItemIndex--; if (currentItemIndex < 0) currentItemIndex = getMenuSize(currentMenuIndex) - 1;
    } else if (key == SDLK_RIGHT) {
        currentMenuIndex++; if (currentMenuIndex >= M_COUNT) currentMenuIndex = 0; currentItemIndex = 0;
    } else if (key == SDLK_LEFT) {
        currentMenuIndex--; if (currentMenuIndex < 0) currentMenuIndex = M_COUNT - 1; currentItemIndex = 0;
    } else if (key == SDLK_RETURN) { processMenuAction();
    } else { checkMenuHotkey(key); }
}

struct MenuLayout {
    int id;
    int x_start;
    int width;
    int box_x;
    int box_y;
    int box_w;
    int box_h;
};

// Define the layout
MenuLayout getLayout(int menuIdx) {
    int rx = eng.SCREEN_COLS - 20;
    switch(menuIdx) {
    case M_SYSTEM:  return {M_SYSTEM,  2,  3,  1, 2, 28, 4};
    case M_FILE:    return {M_FILE,    5,  6,  4, 2, 21, 13};
    case M_EDIT:    return {M_EDIT,    11, 6,  10, 2, 30, 12};
    case M_SEARCH:  return {M_SEARCH,  17, 8,  16, 2, 31, 10};
    case M_RUN:     return {M_RUN,     25, 5,  24, 2, 29, 8};
    case M_COMPILE: return {M_COMPILE, 30, 9,  28, 2, 29, 9};
    case M_DEBUG:   return {M_DEBUG,   39, 7,  38, 2, 36, 9};
    case M_PROJECT: return {M_PROJECT, 46, 9,  46, 2, 26, 10};
    case M_OPTIONS: return {M_OPTIONS, 55, 9,  55, 2, 24, 11};
    case M_WINDOW:  return {M_WINDOW,  rx+4, 8, rx+4, 2, 24, 12};
    case M_HELP:    return {M_HELP,    rx+12,6, rx+12, 2, 30, 9};
    }
    return {0,0,0,0,0,0,0};
}

int mapYToItem(int menuIdx, int y) {
    if (y < 2) return -1; // Border/Shadow

    switch (menuIdx) {
    case M_FILE:
        if (y <= 6) return y - 2;
        if (y == 7) return -1;
        if (y <= 10) return y - 3;
        if (y == 11) return -1;
        if (y == 12) return 8;
        break;
    case M_EDIT:
        if (y <= 3) return y - 2;
        if (y == 4) return -1;
        if (y <= 7) return y - 3;
        if (y <= 9) return y - 3;
        if (y == 10) return -1;
        if (y == 11) return 7;
        break;
    case M_SEARCH:
        if (y <= 4) return y - 2;
        if (y == 5) return -1;
        if (y == 6) return 3;
        if (y <= 8) return y - 3;
        if (y == 9) return 6;
        break;
    case M_COMPILE:
        if (y <= 5) return y - 2;
        if (y == 6) return -1;
        if (y <= 8) return y - 3;
        break;
    case M_DEBUG:
        if (y <= 6) return y - 2;
        if (y == 7) return -1;
        if (y == 8) return 5;
        break;
    case M_PROJECT:
        if (y <= 3) return y - 2;
        if (y == 4) return -1;
        if (y <= 6) return y - 3;
        if (y == 7) return -1;
        if (y <= 9) return y - 4;
        break;
    case M_OPTIONS:
        if (y <= 7) return y - 2;
        if (y == 8) return -1;
        if (y <= 10) return y - 3;
        break;
    case M_WINDOW:
        if (y <= 3) return y - 2;
        if (y == 4) return -1;
        if (y <= 8) return y - 3;
        if (y == 9) return -1;
        if (y <= 11) return y - 4;
        break;
    case M_HELP:
        if (y <= 5) return y - 2;
        if (y == 6) return 4;
        if (y == 7) return -1;
        if (y == 8) return 5;
        break;
    case M_SYSTEM:
    case M_RUN:
        return y - 2;
    }
    return -1;
}

void HandleMenuHover(int gx, int gy) {
    if (currentMenuIndex == -1) return;

    if (gy == 1) {
        for (int i = 0; i < M_COUNT; i++) {
            MenuLayout l = getLayout(i);
            if (i >= M_WINDOW) {
                std::string rightPart = "     indow   elp";
                int rightX = eng.SCREEN_COLS - rightPart.length() - 1;
                if (i == M_WINDOW) l.x_start = rightX + 4;
                if (i == M_HELP) l.x_start = rightX + 12;
            }
            if (gx >= l.x_start && gx < l.x_start + l.width) {
                if (currentMenuIndex != i) {
                    currentMenuIndex = i;
                    currentItemIndex = 0;
                }
                return;
            }
        }
    }

    MenuLayout l = getLayout(currentMenuIndex);

    // FIX: Match dynamic box X for M_WINDOW to rendering logic (COLS - 25)
    if (currentMenuIndex >= M_WINDOW) {
        if (currentMenuIndex == M_WINDOW) {
            l.box_x = eng.SCREEN_COLS - 25;
            if(l.box_x < 0) l.box_x = 0;
            l.box_w = 24; l.box_h = 12;
        }
        if (currentMenuIndex == M_HELP) {
            l.box_x = eng.SCREEN_COLS - 30;
            if(l.box_x < 0) l.box_x=0;
            l.box_w = 30; l.box_h = 9;
        }
    }

    if (gx >= l.box_x && gx < l.box_x + l.box_w && gy >= l.box_y && gy < l.box_y + l.box_h) {
        int relative_y = gy - l.box_y + 1;
        int item_idx = mapYToItem(currentMenuIndex, relative_y);
        int menu_size = getMenuSize(currentMenuIndex);

        if (item_idx >= 0 && item_idx < menu_size) {
            currentItemIndex = item_idx;
        }
    }
}

bool HandleMouseUI(int gx, int gy) {
    if (gy == 1) {
        for (int i = 0; i < M_COUNT; i++) {
            MenuLayout l = getLayout(i);
            if (i >= M_WINDOW) {
                std::string rightPart = "     indow   elp";
                int rightX = eng.SCREEN_COLS - rightPart.length() - 1;
                if (i == M_WINDOW) l.x_start = rightX + 4;
                if (i == M_HELP) l.x_start = rightX + 12;
            }
            if (gx >= l.x_start && gx < l.x_start + l.width) {
                if (currentMenuIndex == i) currentMenuIndex = -1;
                else { currentMenuIndex = i; currentItemIndex = 0; }
                return true;
            }
        }
        return true;
    }

    if (currentMenuIndex != -1) {
        MenuLayout l = getLayout(currentMenuIndex);

        // FIX: Match dynamic box X for M_WINDOW to rendering logic (COLS - 25)
        if (currentMenuIndex >= M_WINDOW) {
            if (currentMenuIndex == M_WINDOW) {
                l.box_x = eng.SCREEN_COLS - 25;
                if(l.box_x < 0) l.box_x = 0;
                l.box_w = 24; l.box_h = 12;
            }
            if (currentMenuIndex == M_HELP) {
                l.box_x = eng.SCREEN_COLS - 30;
                if(l.box_x < 0) l.box_x=0;
                l.box_w = 30; l.box_h = 9;
            }
        }

        if (gx >= l.box_x && gx < l.box_x + l.box_w && gy >= l.box_y && gy < l.box_y + l.box_h) {
            int relative_y = gy - l.box_y + 1;
            int clicked_item_idx = mapYToItem(currentMenuIndex, relative_y);
            int menu_size = getMenuSize(currentMenuIndex);

            if (clicked_item_idx >= 0 && clicked_item_idx < menu_size) {
                currentItemIndex = clicked_item_idx;
                processMenuAction();
                return true;
            }
        } else {
            currentMenuIndex = -1;
            return false;
        }
        return true;
    }
    return false;
}

void RenderUI() {
    MENU(currentMenuIndex != -1);
    wm.render();
    if(openW) openFileWindow();

    if (currentMenuIndex != -1) {
        eng.hide_hw_cursor();
        if(currentMenuIndex == M_SYSTEM) defaultMenu();
        if(currentMenuIndex == M_FILE) fileMenu();
        if(currentMenuIndex == M_EDIT) editMenu();
        if(currentMenuIndex == M_SEARCH) searchMenu();
        if(currentMenuIndex == M_RUN) runMenu();
        if(currentMenuIndex == M_COMPILE) compileMenu();
        if(currentMenuIndex == M_DEBUG) debugMenu();
        if(currentMenuIndex == M_PROJECT) projectMenu();
        if(currentMenuIndex == M_OPTIONS) optionsMenu();
        if(currentMenuIndex == M_WINDOW) windowMenu();
        if(currentMenuIndex == M_HELP) helpMenu();
    }
    drawStatusBar();
}
