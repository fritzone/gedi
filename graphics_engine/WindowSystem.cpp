#include "WindowSystem.h"
#include "GlobalState.h"
#include "BorlandEngine.h"
#include "Utils.h"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

// =============================================================
// HELPER: Fixed Width Button for Dialogs (Clean Shadow)
// =============================================================
void drawFixedButton(int x, int y, int w, const char* text, bool active, char hotkey, bool isDefault) {
    // 1. Draw Button Face
    for (int i = 0; i < w; i++) {
        eng.put_raw(x + i, y, ' ');
        int idx = (y-1)*eng.SCREEN_COLS + (x+i-1);
        if (idx >= 0 && idx < eng.buffer.size()) eng.buffer[idx].bg_index = GREEN;
    }

    // Center Text
    int textLen = strlen(text);
    int textX = x + (w - textLen) / 2;

    for(int i=0; i<textLen; i++) {
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


    // 2. Draw Shadow (Simple Black Shadow)
    // Bottom Shadow
    for(int i=1; i<=w; i++) {
        eng.put_raw(x + i, y + 1, 223); // Lower half block
        int idx = y*eng.SCREEN_COLS + (x+i-1);
        if (idx >= 0 && idx < eng.buffer.size()) {
            eng.buffer[idx].fg_index = BLACK;
            eng.buffer[idx].bg_index = LIGHTGRAY; // Transparent-ish look against dialog bg
        }
    }
    // Right Shadow
    //eng.put_raw(x + w, y, 220); // Bottom half block used vertically? No, 220 is bottom half.
    // Use 221 (Vertical bar left side) or just a dark block
    eng.put_raw(x + w, y, 220); // Using 220 looks like a drop shadow

    int idx = (y-1)*eng.SCREEN_COLS + (x+w-1);
    if(idx >= 0 && idx < eng.buffer.size()) {
        eng.buffer[idx].fg_index = BLACK;
        eng.buffer[idx].bg_index = LIGHTGRAY;
    }
/*
    int idxSide = (y-1)*eng.SCREEN_COLS + (x+w-1);
    if(idxSide >= 0 && idxSide < eng.buffer.size()) {
        eng.buffer[idxSide].fg_index = BLACK;
        eng.buffer[idxSide].bg_index = BLACK;
    }
    /*
    int idxCorner = y*eng.SCREEN_COLS + (x+w-1);
    if(idxCorner >= 0 && idxCorner < eng.buffer.size()) {
        eng.buffer[idxCorner].fg_index = BLACK;
        eng.buffer[idxCorner].bg_index = BLACK;
    }*/
}

// =============================================================
// SYNTAX HIGHLIGHTING HELPERS
// =============================================================

static const std::unordered_set<std::string> cpp_keywords = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
    "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
    "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
    "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
    "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
    "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
    "if", "import", "inline", "int", "long", "module", "mutable", "namespace",
    "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq",
    "private", "protected", "public", "register", "reinterpret_cast", "requires",
    "return", "short", "signed", "sizeof", "static", "static_assert",
    "static_cast", "struct", "switch", "template", "this", "thread_local",
    "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned",
    "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq"
};

enum TokenType {
    TOK_NORMAL, TOK_KEYWORD, TOK_STRING, TOK_COMMENT, TOK_NUMBER, TOK_PREPROC, TOK_SYMBOL
};

// Render a single line with highlighting, clipped to the view
void renderSyntaxLine(int start_x, int y, int width, const std::string& line, int scroll_x) {
    if (line.empty()) return;

    int len = line.length();
    int i = 0;

    while (i < len) {
        int token_start = i;
        TokenType type = TOK_NORMAL;
        int color = YELLOW;

        char c = line[i];

        if (isspace(c)) { i++; type = TOK_NORMAL; }
        else if (c == '/' && i + 1 < len && line[i+1] == '/') { type = TOK_COMMENT; color = LIGHTGRAY; i = len; }
        else if (c == '"') {
            type = TOK_STRING; color = LIGHTRED;
            i++; while (i < len) { if (line[i] == '"' && line[i-1] != '\\') { i++; break; } i++; }
        }
        else if (c == '\'') {
            type = TOK_STRING; color = LIGHTRED;
            i++; while (i < len) { if (line[i] == '\'' && line[i-1] != '\\') { i++; break; } i++; }
        }
        else if (c == '#') {
            type = TOK_PREPROC; color = LIGHTGREEN;
            i++; while (i < len && isalpha(line[i])) i++;
        }
        else if (isalpha(c) || c == '_') {
            while (i < len && (isalnum(line[i]) || line[i] == '_')) i++;
            std::string word = line.substr(token_start, i - token_start);
            if (cpp_keywords.count(word)) { type = TOK_KEYWORD; color = WHITE; }
            else { type = TOK_NORMAL; color = YELLOW; }
        }
        else if (isdigit(c)) {
            type = TOK_NUMBER; color = LIGHTCYAN;
            while (i < len && (isalnum(line[i]) || line[i] == '.')) i++;
        }
        else { type = TOK_SYMBOL; color = LIGHTGREEN; i++; }

        int draw_start = std::max(token_start, scroll_x);
        int draw_end = std::min(i, scroll_x + width);

        if (draw_start < draw_end) {
            int screen_draw_x = start_x + (draw_start - scroll_x);
            std::string sub = line.substr(draw_start, draw_end - draw_start);
            gotoxy(screen_draw_x, y);
            textcolor(color);
            cprintf("%s", sub.c_str());
        }
    }
}

// =============================================================
// CLASS IMPLEMENTATIONS
// =============================================================

// --- Window ---
Window::Window(int _x, int _y, int _w, int _h, const std::string& _title)
    : x(_x), y(_y), w(_w), h(_h), title(_title) {
    static int next_id = 1; id = next_id++;
    restore_x = x; restore_y = y; restore_w = w; restore_h = h;
}

void Window::toggleZoom() {
    if (!is_maximized) {
        restore_x = x; restore_y = y; restore_w = w; restore_h = h;
        x = 1; y = 2; w = eng.SCREEN_COLS; h = eng.SCREEN_ROWS - 2;
        is_maximized = true;
    } else {
        x = restore_x; y = restore_y; w = restore_w; h = restore_h;
        is_maximized = false;
    }
}

// --- EditorWindow ---
EditorWindow::EditorWindow(int _x, int _y, int _w, int _h, const std::string& _title)
    : Window(_x, _y, _w, _h, _title) {
    lines.push_back("");
    updateViewSize();
}

void EditorWindow::updateViewSize() { view_w = w - 2; view_h = h - 2; }

void EditorWindow::saveToFile(const std::string& path) {
    std::ofstream out(fullPath.size() == 0 ? path : fullPath);
    if (out.is_open()) {
        for(const auto& l : lines) out << l << "\n";
        out.close();

        // Store full path correctly
        fullPath = fullPath.size() == 0 ? path : fullPath;
        isModified = false;

        // Update Title (Filename only, no uppercase)
        size_t last_slash = fullPath.find_last_of("/\\");
        if (last_slash != std::string::npos) title = fullPath.substr(last_slash + 1);
        else title = path;

    }
}


void EditorWindow::handleTextInput(const char* text) {
    if (cursor_x > (int)lines[cursor_y].length()) {
        int gaps = cursor_x - (int)lines[cursor_y].length();
        lines[cursor_y].append(gaps, ' ');
    }
    lines[cursor_y].insert(cursor_x, text);
    cursor_x += strlen(text);
    isModified = true; // Mark as modified
    scrollToCursor();
}

bool EditorWindow::handleMouseClick(int gx, int gy) {

    if (gy == y && gx >= x + 1 && gx <= x + 3) {
        wm.closeActiveWindow();
        return true;
    }

    if (gy == y && gx == x + w - 4) {
        toggleZoom();
        return true;
    }

    int content_x1 = x + 1;
    int content_y1 = y + 1;
    int content_x2 = x + w - 2;
    int content_y2 = y + h - 2;

    if (gx >= content_x1 && gx <= content_x2 && gy >= content_y1 && gy <= content_y2) {
        int rel_x = gx - content_x1;
        int rel_y = gy - content_y1;

        int target_y = scroll_y + rel_y;
        int target_x = scroll_x + rel_x;

        if (target_y < 0) target_y = 0;
        if (target_y >= (int)lines.size()) target_y = lines.size() - 1;
        if (target_x < 0) target_x = 0;

        cursor_x = target_x;
        cursor_y = target_y;
        scrollToCursor();
        return true;
    }
    return false;
}

void EditorWindow::handleInput(SDL_Keycode key, Uint16 mod) {
    bool ctrl = (mod & KMOD_CTRL);

    if (key == SDLK_HOME) {
        if (ctrl) { cursor_y = 0; cursor_x = 0; } else { cursor_x = 0; }
    }
    else if (key == SDLK_END) {
        if (ctrl) { cursor_y = lines.size() - 1; cursor_x = lines[cursor_y].length(); }
        else { cursor_x = lines[cursor_y].length(); }
    }
    else if (key == SDLK_PAGEUP) {
        cursor_y -= view_h; if (cursor_y < 0) cursor_y = 0;
    }
    else if (key == SDLK_PAGEDOWN) {
        cursor_y += view_h; if (cursor_y >= (int)lines.size()) cursor_y = lines.size() - 1;
    }
    else if (key == SDLK_DELETE) {
        if (cursor_x < (int)lines[cursor_y].length()) {
            isModified = true; // Mark as modified
            lines[cursor_y].erase(cursor_x, 1);
        }
        else if (cursor_y < (int)lines.size() - 1) {
            std::string next_line = lines[cursor_y + 1];
            lines[cursor_y] += next_line;
            lines.erase(lines.begin() + cursor_y + 1);
            isModified = true; // Mark as modified
        }
    }
    else if (key == SDLK_BACKSPACE) {
        if (cursor_x > (int)lines[cursor_y].length()) { cursor_x--; }
        else if (cursor_x > 0) {
            lines[cursor_y].erase(cursor_x - 1, 1); cursor_x--;
            isModified = true; // Mark as modified
        }
        else if (cursor_y > 0) {
            int prev_len = lines[cursor_y - 1].length();
            lines[cursor_y - 1] += lines[cursor_y];
            lines.erase(lines.begin() + cursor_y);
            cursor_y--; cursor_x = prev_len;
            isModified = true; // Mark as modified
        }
    } else if (key == SDLK_RETURN) {
        std::string current = lines[cursor_y];
        std::string next_line = "";
        if (cursor_x < (int)current.length()) {
            next_line = current.substr(cursor_x);
            lines[cursor_y] = current.substr(0, cursor_x);
            isModified = true; // Mark as modified
        }
        lines.insert(lines.begin() + cursor_y + 1, next_line);
        cursor_y++; cursor_x = 0;
    } else if (key == SDLK_LEFT) {
        if (cursor_x > 0) cursor_x--;
        else if (cursor_y > 0) { cursor_y--; cursor_x = lines[cursor_y].length(); }
    } else if (key == SDLK_RIGHT) { cursor_x++; }
    else if (key == SDLK_UP) { if (cursor_y > 0) cursor_y--; }
    else if (key == SDLK_DOWN) { if (cursor_y < (int)lines.size() - 1) cursor_y++; }
    scrollToCursor();
}

void EditorWindow::onResize(int screenCols, int screenRows) {
    Window::onResize(screenCols, screenRows);
    updateViewSize();
    scrollToCursor();
}

void EditorWindow::scrollToCursor() {
    updateViewSize();
    if (cursor_y < scroll_y) scroll_y = cursor_y;
    if (cursor_y >= scroll_y + view_h) scroll_y = cursor_y - view_h + 1;
    if (cursor_x < scroll_x) scroll_x = cursor_x;
    if (cursor_x >= scroll_x + view_w) scroll_x = cursor_x - view_w + 1;
}

void EditorWindow::render(bool isActive, bool allowCursor) {
    updateViewSize();
    eng.make_shadow(x + 2, y + 1, w, h);
    window(x, y, x + w - 1, y + h - 1);
    textbackground(BLUE); clrscr();

    if (isActive) {
        textcolor(WHITE); drawFrame(h, w, 1);
        gotoxy(2, 1); textbackground(BLUE); textcolor(WHITE); cprintf("[%c]", 254);
        gotoxy(3, 1); textcolor(LIGHTGREEN); cprintf("%c", 254);
        std::string displayTitle = (isModified ? "* " : "") + title;
        int title_start = (w - displayTitle.length()) / 2;
        if(title_start < 4) title_start = 4;

        textbackground(BLUE); textcolor(WHITE);
        gotoxy(title_start - 2, 1); cprintf(" %s ", displayTitle.c_str());
        gotoxy(w - 7, 1); textbackground(BLUE); textcolor(WHITE); cprintf("%c%d%c[%c]%c", 205, id, 205, ' ', 205);

        // Draw Zoom Icon based on State (24=Up, 18=Restore/UpDown)
        unsigned char zoomChar = is_maximized ? 18 : 24;
        gotoxy(w - 3, 1); textcolor(LIGHTGREEN); cprintf("%c", zoomChar);

        char coord[32];
        sprintf(coord, " %d:%d ", cursor_y + 1, cursor_x + 1);
        int coord_len = strlen(coord);

        gotoxy(2, h);
        textcolor(BLACK); textbackground(LIGHTGRAY);
        cprintf("%s", coord);

        int left_arrow_x = 2 + coord_len;
        int right_arrow_x = w - 1;

        textcolor(WHITE);
        gotoxy(w, 2); cprintf("%c", 30);
        gotoxy(w, h-1); cprintf("%c", 31);

        if (left_arrow_x < right_arrow_x) {
            gotoxy(left_arrow_x, h); cprintf("%c", 17);
            gotoxy(right_arrow_x, h); cprintf("%c", 16);
        }

        int v_track_h = h - 4;
        int v_track_x = w;
        int v_track_y_start = 3;

        if (v_track_h > 0) {
            int total_lines = lines.size();
            if (total_lines == 0) total_lines = 1;
            int viewport_h = view_h;
            int thumb_size = (int)((float)viewport_h / total_lines * v_track_h);
            if (thumb_size < 1) thumb_size = 1;
            if (thumb_size > v_track_h) thumb_size = v_track_h;
            int thumb_pos = 0;
            int max_scroll = total_lines - viewport_h;
            if (max_scroll > 0) {
                float pct = (float)scroll_y / max_scroll;
                thumb_pos = (int)(pct * (v_track_h - thumb_size));
                if (thumb_pos < 0) thumb_pos = 0;
                if (thumb_pos + thumb_size > v_track_h) thumb_pos = v_track_h - thumb_size;
            }
            for (int i = 0; i < v_track_h; i++) {
                gotoxy(v_track_x, v_track_y_start + i);
                if (i >= thumb_pos && i < thumb_pos + thumb_size) {
                    textcolor(WHITE); textbackground(LIGHTGRAY); cprintf("%c", 219);
                } else {
                    textcolor(BLACK); textbackground(LIGHTGRAY); cprintf("%c", 176);
                }
            }
        }

        int h_track_x_start = left_arrow_x + 1;
        int h_track_w = right_arrow_x - h_track_x_start;
        int h_track_y = h;

        if (h_track_w > 0) {
            size_t max_len = 0;
            for(const auto& line : lines) {
                if(line.length() > max_len) max_len = line.length();
            }
            if ((size_t)cursor_x > max_len) max_len = cursor_x;

            size_t total_width = std::max((size_t)view_w, max_len);
            int thumb_size = (int)((float)view_w / total_width * h_track_w);
            if (thumb_size < 1) thumb_size = 1;
            if (thumb_size > h_track_w) thumb_size = h_track_w;
            int thumb_pos = 0;
            size_t max_scroll_x = total_width - view_w;
            if (max_scroll_x > 0) {
                float pct = (float)scroll_x / max_scroll_x;
                thumb_pos = (int)(pct * (h_track_w - thumb_size));
                if (thumb_pos < 0) thumb_pos = 0;
                if (thumb_pos + thumb_size > h_track_w) thumb_pos = h_track_w - thumb_size;
            }
            for (int i = 0; i < h_track_w; i++) {
                gotoxy(h_track_x_start + i, h_track_y);
                if (i >= thumb_pos && i < thumb_pos + thumb_size) {
                    textcolor(WHITE); textbackground(LIGHTGRAY); cprintf("%c", 219);
                } else {
                    textcolor(BLACK); textbackground(LIGHTGRAY); cprintf("%c", 176);
                }
            }
        }

    } else {
        textcolor(LIGHTGRAY); drawFrame(h, w, 0);
        int title_start = (w - title.length()) / 2;
        if(title_start < 4) title_start = 4;
        textbackground(BLUE); textcolor(LIGHTGRAY); gotoxy(title_start - 2, 1); cprintf("- %s -", title.c_str());
        gotoxy(w - 5, 1); cprintf("-%d-", id);
    }

    window(x + 1, y + 1, x + w - 2, y + h - 2);
    textbackground(BLUE);
    if(isActive) textcolor(YELLOW); else textcolor(LIGHTGRAY);

    for (int i = 0; i < view_h; ++i) {
        int line_idx = scroll_y + i;
        gotoxy(1, i + 1); line(view_w, ' ');
        if (line_idx < (int)lines.size()) {
            std::string l = lines[line_idx];
            renderSyntaxLine(1, i + 1, view_w, l, scroll_x);
        }
    }

    if (isActive && allowCursor && !isResizing) {
        eng.set_hw_cursor((cursor_x - scroll_x) + 1, (cursor_y - scroll_y) + 1);
    }
}

// --- FileDialog ---
FileDialog::FileDialog() {
    try { currentPath = fs::current_path().string(); } catch(...) { currentPath = "."; }
    inputBuffer = currentPath;
    refreshFiles();
}

void FileDialog::center() {
    X = (eng.SCREEN_COLS - W) / 2; Y = (eng.SCREEN_ROWS - H) / 2;
}

void FileDialog::refreshFiles() {
    fileList.clear();
    try {
        fileList.push_back("..");
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(currentPath)) {
            if (entry.is_directory()) fileList.push_back(entry.path().filename());
            else files.push_back(entry.path().filename());
        }
        std::sort(fileList.begin() + 1, fileList.end());
        std::sort(files.begin(), files.end());
        fileList.insert(fileList.end(), files.begin(), files.end());
    } catch (...) {}
    selectedFileIndex = 0; listScroll = 0;
}

void FileDialog::open(Mode m, std::function<void(std::string)> callback) {
    active = true;
    currentFocus = FOCUS_INPUT;
    inputBuffer = currentPath;
    mode = m;
    onComplete = callback;

#ifdef _WIN32
    if (inputBuffer.back() != '\\') inputBuffer += "\\";
#else
    if (inputBuffer.back() != '/') inputBuffer += "/";
#endif
    inputBuffer += "*.CPP";
    center();
}


void FileDialog::render() {
    if (!active) return;
    center();
    eng.make_shadow(X + 2, Y + 1, W, H);
    window(X, Y, X + W - 1, Y + H - 1);
    textbackground(LIGHTGRAY); clrscr();
    textcolor(WHITE); drawFrame(H, W, 1);

    std::string title = (mode == MODE_OPEN) ? " Open File " : " Save File As ";
    gotoxy((W - title.length()) / 2, 1); textbackground(LIGHTGRAY); textcolor(BLACK); cprintf("%s", title.c_str());

    gotoxy(2, 1); cprintf("[%c]", 254); gotoxy(3, 1); textcolor(GREEN); cprintf("%c", 254);

    gotoxy(3, 2); textcolor(YELLOW); textbackground(LIGHTGRAY);
    cprintf("%s", (mode == MODE_OPEN) ? "Open File" : "Save File As");
    int inputX = 3, inputY = 3;

    // REDUCE input width to avoid overlap with buttons
    int inputW = W - 20;

    gotoxy(inputX, inputY);
    textbackground(BLUE); textcolor(WHITE);
    std::string displayInput = inputBuffer;
    if ((int)displayInput.length() >= inputW) displayInput = displayInput.substr(displayInput.length() - (inputW - 1));
    std::string pad(inputW, ' '); cprintf("%s", pad.c_str());
    gotoxy(inputX, inputY); cprintf("%s", displayInput.c_str());

    // FIX: Cursor Position relative to Viewport
    if (currentFocus == FOCUS_INPUT) {
        eng.set_hw_cursor(inputX + (int)displayInput.length(), inputY);
    } else {
        eng.hide_hw_cursor();
    }

    gotoxy(3, 5); textcolor(YELLOW); textbackground(LIGHTGRAY); cprintf("Files");

    // File List Dimensions
    int listX = 3;
    int listY = 6;
    int listW = inputW; // Match input width
    int listH = H - 9;

    // Draw Frame for List
    window(X, Y, X + W - 1, Y + H - 1); // Dialog Context

    int frameX = listX - 1;
    int frameY = listY - 1;
    int frameW = listW + 2;
    int frameH = listH + 2;

    textcolor(BLACK); textbackground(LIGHTGRAY);

    // Top & Bottom Border
    gotoxy(frameX, frameY); cprintf("%c", 218);
    for(int i=0; i<frameW-2; i++) cprintf("%c", 196);
    cprintf("%c", 191);

    gotoxy(frameX, frameY + frameH - 1); cprintf("%c", 192);
    for(int i=0; i<frameW-2; i++) cprintf("%c", 196);
    cprintf("%c", 217);

    // Sides
    for(int i=1; i<frameH-1; i++) {
        gotoxy(frameX, frameY + i); cprintf("%c", 179);
        gotoxy(frameX + frameW - 1, frameY + i); cprintf("%c", 179);
    }

    // Scrollbar (On Right Border)
    int scrollX = frameX + frameW - 1;
    int scrollY = frameY + 1;
    int scrollH = frameH - 2;

    // Arrows
    textcolor(WHITE); textbackground(LIGHTGRAY);
    gotoxy(scrollX, scrollY); cprintf("%c", 30);
    gotoxy(scrollX, scrollY + scrollH - 1); cprintf("%c", 31);

    // Track
    int trackH = scrollH - 2;
    int trackY = scrollY + 1;

    for(int i=0; i<trackH; i++) {
        gotoxy(scrollX, trackY + i);
        textcolor(BLACK); textbackground(LIGHTGRAY); cprintf("%c", 176);
    }

    if (!fileList.empty() && fileList.size() > listH) {
        int maxScroll = fileList.size() - listH;
        int thumbSize = std::max(1, (int)((float)listH / fileList.size() * trackH));
        int thumbPos = (int)((float)listScroll / maxScroll * (trackH - thumbSize));

        for(int i=0; i<thumbSize; i++) {
            gotoxy(scrollX, trackY + thumbPos + i);
            textcolor(WHITE); textbackground(LIGHTGRAY); cprintf("%c", 219);
        }
    }

    // List Content
    window(X + listX - 1, Y + listY - 1, X + listX + listW - 2, Y + listY + listH - 2);
    textbackground(CYAN); clrscr();

    for (int i = 0; i < listH; ++i) {
        int idx = listScroll + i;
        gotoxy(1, i + 1);
        if (idx < fileList.size()) {
            if (idx == selectedFileIndex) { textbackground(BLUE); textcolor(WHITE); }
            else { textbackground(CYAN); textcolor(BLACK); }

            std::string fname = fileList[idx].string();
            if (fname.length() > listW) fname = fname.substr(0, listW - 3) + "...";

            int padLen = listW - fname.length();
            if (padLen < 0) padLen = 0;
            std::string p(padLen, ' ');
            cprintf("%s%s", fname.c_str(), p.c_str());
        } else {
            textbackground(CYAN);
            std::string p(listW, ' ');
            cprintf("%s", p.c_str());
        }
    }

    // Buttons
    window(X, Y, X + W - 1, Y + H - 1);
    int btnX = X + W - 14;
    drawFixedButton(btnX, Y + 4, 10, "OK", currentFocus == FOCUS_OK, 'O', true);
    drawFixedButton(btnX, Y + 8, 10, "Cancel", currentFocus == FOCUS_CANCEL, 'C', false);
    drawFixedButton(btnX, Y + 12, 10, "Help", currentFocus == FOCUS_HELP, 'H', false);

    window(X + 1, Y + H - 2, X + W - 2, Y + H - 2);
    textbackground(BLUE); textcolor(LIGHTGRAY); clrscr();
    gotoxy(1, 1);
    std::string bottomText = inputBuffer;
    if (bottomText.length() > W - 4) bottomText = bottomText.substr(0, W - 4);
    cprintf(" %s", bottomText.c_str());
}


void EditorWindow::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (in.is_open()) {
        lines.clear();
        std::string line;
        while(std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
        if (lines.empty()) lines.push_back("");
        in.close();

        // Store full path
        fullPath = path;

        // Reset modification flag
        isModified = false;

        // Update Title (Filename only, no uppercase)
        size_t last_slash = path.find_last_of("/\\");
        if (last_slash != std::string::npos) title = path.substr(last_slash + 1);
        else title = path;

        cursor_x = 0; cursor_y = 0;
        scroll_x = 0; scroll_y = 0;
        updateViewSize();
    }
}
void FileDialog::actionOK() {
    fs::path p(inputBuffer);
    if (p.has_parent_path()) {
        if (fs::is_directory(p)) {
            currentPath = fs::absolute(p).string();
#ifdef _WIN32
            inputBuffer = currentPath + "\\*.CPP";
#else
            inputBuffer = currentPath + "/*.CPP";
#endif
            refreshFiles(); return;
        }
    } else p = fs::path(currentPath) / p;

    if (inputBuffer.find('*') != std::string::npos) { refreshFiles(); return; }

    // Execute callback with the final path
    if (onComplete) onComplete(p.string());

    active = false; eng.hide_hw_cursor();
}

// UPDATED: removed editor ptr argument
void FileDialog::handleInput(SDL_Keycode key, Uint16 mod) {
    if (key == SDLK_ESCAPE) { active = false; eng.hide_hw_cursor(); return; }
    if (currentFocus == FOCUS_INPUT) {
        if (key == SDLK_TAB) currentFocus = FOCUS_LIST;
        else if (key == SDLK_RETURN) actionOK();
        else if (key == SDLK_BACKSPACE && !inputBuffer.empty()) inputBuffer.pop_back();
    } else if (currentFocus == FOCUS_LIST) {
        if (key == SDLK_TAB) currentFocus = FOCUS_OK;
        else if (key == SDLK_UP && selectedFileIndex > 0) {
            selectedFileIndex--; if (selectedFileIndex < listScroll) listScroll = selectedFileIndex;
        } else if (key == SDLK_DOWN && selectedFileIndex < fileList.size() - 1) {
            selectedFileIndex++; if (selectedFileIndex >= listScroll + (H - 10)) listScroll++;
        } else if (key == SDLK_RETURN) {
            fs::path selected = fileList[selectedFileIndex];
            if (selected == "..") {
                currentPath = fs::path(currentPath).parent_path().string();
                refreshFiles();
#ifdef _WIN32
                inputBuffer = currentPath + "\\*.CPP";
#else
                inputBuffer = currentPath + "/*.CPP";
#endif
            } else {
                fs::path full = fs::path(currentPath) / selected;
                if (fs::is_directory(full)) {
                    currentPath = full.string();
                    refreshFiles();
#ifdef _WIN32
                    inputBuffer = currentPath + "\\*.CPP";
#else
                    inputBuffer = currentPath + "/*.CPP";
#endif
                } else {
                    inputBuffer = full.string(); currentFocus = FOCUS_INPUT;
                }
            }
        }
    } else {
        if (key == SDLK_TAB) {
            if (currentFocus == FOCUS_OK) currentFocus = FOCUS_CANCEL;
            else if (currentFocus == FOCUS_CANCEL) currentFocus = FOCUS_HELP;
            else currentFocus = FOCUS_INPUT;
        } else if (key == SDLK_RETURN) {
            if (currentFocus == FOCUS_OK) actionOK();
            else if (currentFocus == FOCUS_CANCEL) active = false;
        }
    }
}


void FileDialog::handleTextInput(const char* text) {
    if (currentFocus == FOCUS_INPUT) inputBuffer += text;
}

// --- WindowManager ---
WindowManager::WindowManager() {
    fileDialog = std::make_shared<FileDialog>();
    questionDialog = std::make_shared<QuestionDialog>();
}

void WindowManager::addWindow(std::shared_ptr<Window> win) {
    windows.push_back(win); activeWindowIndex = windows.size() - 1;
}

void WindowManager::closeActiveWindow() {
    auto win = getActiveWindow();
    if (!win) return;

    auto editor = std::dynamic_pointer_cast<EditorWindow>(win);

    if (editor && editor->isModified) {
        questionDialog->open(
            "File has changed. Save?",
            [this]() { // Yes -> Save then Close
                this->requestSave(false, [this]() {
                    this->forceCloseActiveWindow();
                });
            },
            [this]() { this->forceCloseActiveWindow(); }, // No -> Close without saving
            []() { } // Cancel (do nothing)
            );
    } else {
        forceCloseActiveWindow();
    }
}

void WindowManager::forceCloseActiveWindow() {
    if (activeWindowIndex >= 0 && activeWindowIndex < windows.size()) {
        windows.erase(windows.begin() + activeWindowIndex);
        if (activeWindowIndex >= windows.size()) activeWindowIndex = windows.size() - 1;
    }
}

void WindowManager::closeAll() { windows.clear(); activeWindowIndex = -1; }
void WindowManager::nextWindow() {
    if (windows.empty()) return; activeWindowIndex = (activeWindowIndex + 1) % windows.size();
}
void WindowManager::prevWindow() {
    if (windows.empty()) return; activeWindowIndex--; if (activeWindowIndex < 0) activeWindowIndex = windows.size() - 1;
}
std::shared_ptr<Window> WindowManager::getActiveWindow() {
    if (activeWindowIndex >= 0 && activeWindowIndex < windows.size()) return windows[activeWindowIndex];
    return nullptr;
}
void WindowManager::startResize() {
    auto win = getActiveWindow();
    if (win) {
        isResizing = true;
        resize_orig_x = win->x; resize_orig_y = win->y; resize_orig_w = win->w; resize_orig_h = win->h;
    }
}
void WindowManager::handleResizeInput(SDL_Keycode key, Uint16 mod) {
    auto win = getActiveWindow(); if (!win) return;
    bool shift = (mod & KMOD_SHIFT);
    if (key == SDLK_UP) { if (shift) { if (win->h > 3) win->h--; } else { if (win->y > 2) win->y--; } }
    else if (key == SDLK_DOWN) { if (shift) { if (win->h < eng.SCREEN_ROWS - win->y) win->h++; } else { if (win->y + win->h < eng.SCREEN_ROWS) win->y++; } }
    else if (key == SDLK_LEFT) { if (shift) { if (win->w > 10) win->w--; } else { if (win->x > 1) win->x--; } }
    else if (key == SDLK_RIGHT) { if (shift) { if (win->w < eng.SCREEN_COLS - win->x) win->w++; } else { if (win->x + win->w <= eng.SCREEN_COLS) win->x++; } }
    else if (key == SDLK_RETURN) { isResizing = false; }
    else if (key == SDLK_ESCAPE) { win->x = resize_orig_x; win->y = resize_orig_y; win->w = resize_orig_w; win->h = resize_orig_h; isResizing = false; }
}
void WindowManager::render() {
    for (size_t i = 0; i < windows.size(); ++i) if (i != activeWindowIndex) windows[i]->render(false, false);
    if (activeWindowIndex >= 0 && activeWindowIndex < windows.size()) windows[activeWindowIndex]->render(true, !fileDialog->active);
    else eng.hide_hw_cursor();
    if (fileDialog->active) fileDialog->render();
    if (questionDialog->active) questionDialog->render();

}
bool WindowManager::handleMouse(int gx, int gy) {
    if (fileDialog->active) return true;
    for (int i = windows.size() - 1; i >= 0; --i) {
        auto win = windows[i];
        if (gx >= win->x && gx < win->x + win->w && gy >= win->y && gy < win->y + win->h) {
            activeWindowIndex = i;
            win->handleMouseClick(gx, gy);
            return true;
        }
    }
    return false;
}

// Updated to accept Callback
void WindowManager::requestSave(bool forceDialog, std::function<void()> onSaved) {
    auto win = getActiveWindow(); if (!win) return;
    auto editor = std::dynamic_pointer_cast<EditorWindow>(win); if (!editor) return;

    if (forceDialog || editor->fullPath.empty()) {
        fileDialog->open(FileDialog::MODE_SAVE, [editor, onSaved](std::string path) {
            editor->saveToFile(path);
            if(onSaved) onSaved();
        });
    }
    else {
        editor->saveToFile(editor->fullPath);
        if(onSaved) onSaved();
    }
}

// NEW: Open Logic
void WindowManager::requestOpen() {
    fileDialog->open(FileDialog::MODE_OPEN, [this](std::string path) {
        this->openFile(path);
    });
}

// NEW: File Loading
void WindowManager::openFile(const std::string& path) {
    // 1. Check if already open? (Optional optimization)

    // 2. Create new window
    int w = std::min(60, eng.SCREEN_COLS - 10);
    int h = std::min(15, eng.SCREEN_ROWS - 5);
    // Simple cascade position
    static int offset = 0;
    int x = 5 + offset; int y = 5 + offset;
    offset += 1; if(offset > 5) offset = 0;

    // Title will be updated by loadFromFile
    auto win = std::make_shared<EditorWindow>(x, y, w, h, "LOADING...");
    win->loadFromFile(path);

    addWindow(win);
}

void WindowManager::notifyResize(int cols, int rows) {
    for(auto& win : windows) { win->onResize(cols, rows); }
    if(fileDialog->active) fileDialog->center();
    if(questionDialog->active) questionDialog->center();
}

bool Window::onMouseDown(int gx, int gy) {

    if (gy == y && gx >= x + 1 && gx <= x + 3) {
        wm.closeActiveWindow();
        return true;
    }

    if (is_maximized) return false; // Cannot move/resize when maximized

    // 1. Check for Resize (Bottom Right Corner)
    if (gx == x + w - 1 && gy == y + h - 1) {
        currentDragMode = DRAG_RESIZE;
        dragStartX = gx;
        dragStartY = gy;
        winStartX = x;
        winStartY = y;
        winStartW = w;
        winStartH = h;
        return true;
    }

    // 2. Check for Move (Top Border)
    // Avoid Zoom Button (x+w-3) and Close Button (x+2)
    if (gy == y && gx >= x && gx < x + w) {
        // Exclude the Zoom Icon area [ ]
        if (gx >= x + w - 4) return false;

        currentDragMode = DRAG_MOVE;
        dragStartX = gx;
        dragStartY = gy;
        winStartX = x;
        winStartY = y;
        return true;
    }

    return false;
}

// NEW: Mouse Drag Logic
void Window::onMouseDrag(int gx, int gy) {
    if (currentDragMode == DRAG_MOVE) {
        int dx = gx - dragStartX;
        int dy = gy - dragStartY;
        x = winStartX + dx;
        y = winStartY + dy;
        // Clamp to screen?? For now allow some flexibility
    } else if (currentDragMode == DRAG_RESIZE) {
        int dx = gx - dragStartX;
        int dy = gy - dragStartY;
        w = winStartW + dx;
        h = winStartH + dy;

        // Minimum Size Constraints
        if (w < 10) w = 10;
        if (h < 5) h = 5;
    }
}

void Window::onMouseUp() {
    currentDragMode = DRAG_NONE;
}

void Window::onResize(int screenCols, int screenRows) {
    if (is_maximized) {
        x = 1; y = 2; w = screenCols; h = screenRows - 2;
    } else {
        if (x > screenCols - 2) x = screenCols - 10;
        if (x < 1) x = 1;
        if (y > screenRows - 2) y = screenRows - 5;
        if (y < 2) y = 2;
        if (w > screenCols) w = screenCols;
        if (h > screenRows - 2) h = screenRows - 2;
        if (x + w > screenCols + 1) w = screenCols - x + 1;
        if (y + h > screenRows) h = screenRows - y;
    }
}

void WindowManager::handleMouseDrag(int gx, int gy) {
    if (isDragging) {
        auto win = getActiveWindow();
        if (win) win->onMouseDrag(gx, gy);
    }
}

void WindowManager::handleMouseUp() {
    if (isDragging) {
        auto win = getActiveWindow();
        if (win) win->onMouseUp();
        isDragging = false;
    }
}

bool WindowManager::handleMousePress(int gx, int gy) {
    if (fileDialog->active) return true;
    if (questionDialog->active) return questionDialog->handleMouse(gx, gy); // Route to dialog

    for (int i = windows.size() - 1; i >= 0; --i) {
        auto win = windows[i];
        if (gx >= win->x && gx < win->x + win->w && gy >= win->y && gy < win->y + win->h) {
            activeWindowIndex = i;

            // Check Frame Interactions (Move/Resize)
            if (win->onMouseDown(gx, gy)) {
                isDragging = true;
            }
            // Check Content Interactions (Cursor placement)
            else {
                win->handleMouseClick(gx, gy);
            }
            return true;
        }
    }
    return false;
}



void QuestionDialog::open(const std::string& msg, std::function<void()> yesCb, std::function<void()> noCb, std::function<void()> saveCb) {
    active = true;
    message = msg;
    onYes = yesCb;
    onNo = noCb;
    onSaveFirst = saveCb;
    selectedButton = 2; // Default to Save First
    center();
}

void QuestionDialog::center() {
    X = (eng.SCREEN_COLS - W) / 2;
    Y = (eng.SCREEN_ROWS - H) / 2;
}

void QuestionDialog::render() {
    if (!active) return;
    center(); // Keep centered on resize

    eng.make_shadow(X + 2, Y + 1, W, H);
    window(X, Y, X + W - 1, Y + H - 1);

    textbackground(LIGHTGRAY); clrscr();
    textcolor(BLACK); drawFrame(H, W, 1);

    // Title
    const char* title = " Close Window ";
    gotoxy((W - strlen(title)) / 2, 1);
    textbackground(LIGHTGRAY); textcolor(BLACK);
    cprintf("%s", title);

    // Message (Centered)
    int msgLen = message.length();
    int msgX = (W - msgLen) / 2;
    if (msgX < 2) msgX = 2;
    gotoxy(msgX, 3);
    cprintf("%s", message.c_str());

    // Buttons: Yes (0), No (1), Save First (2)
    // Layout:  [ Yes ]   [ No ]   [ Save First ]
    // We need to space them out.
    // Yes: ~8, No: ~8, Save: ~14. Total ~30. W=45. Space = 15.

    int btnY = Y + H - 5;
    int spacing = 2;
    int totalBtnW = 10 + 2 + 10 + 2 + 14; // Approx
    int startX = X + (W - totalBtnW) / 2;

    drawFixedButton(startX, btnY, 10, "Yes", selectedButton == 0, 'Y', false);
    drawFixedButton(startX + 12, btnY, 10, "No", selectedButton == 1, 'N', false);
    drawFixedButton(startX + 24, btnY, 14, "Cancel", selectedButton == 2, 'S', true); // Default

    // Hide Hardware cursor while dialog is active
    eng.hide_hw_cursor();
}

void QuestionDialog::handleInput(SDL_Keycode key, Uint16 mod) {
    if (key == SDLK_TAB) {
        selectedButton = (selectedButton + 1) % 3;
    }
    else if (key == SDLK_LEFT) {
        selectedButton--; if(selectedButton < 0) selectedButton = 2;
    }
    else if (key == SDLK_RIGHT) {
        selectedButton = (selectedButton + 1) % 3;
    }
    else if (key == SDLK_y) {
        if(onYes) onYes();
        active = false;
    }
    else if (key == SDLK_n) {
        if(onNo) onNo();
        active = false;
    }
    else if (key == SDLK_s) {
        if(onSaveFirst) onSaveFirst();
        active = false;
    }
    else if (key == SDLK_RETURN) {
        if (selectedButton == 0 && onYes) onYes();
        else if (selectedButton == 1 && onNo) onNo();
        else if (selectedButton == 2 && onSaveFirst) onSaveFirst();
        active = false;
    }
    else if (key == SDLK_ESCAPE) {
        if(onNo) onNo(); // Treat Esc as No/Cancel
        active = false;
    }
}

bool QuestionDialog::handleMouse(int gx, int gy) {
    if (!active) return false;

    // Check buttons
    // Re-calculate positions (copy logic from render)
    int btnY = Y + H - 3; // Absolute Y relative to screen is Y + (H-3) ?
    // Wait, render() calls window(), so coordinates in render are relative to the window X,Y.
    // handleMouse receives GRID coordinates (absolute).
    // So we need to compare absolute coords.

    int absY = Y + (H - 3);
    int startX = X + (W - (10 + 2 + 10 + 2 + 14)) / 2;

    // Bounding Box Logic for Buttons
    // Yes: startX, width 10
    if (gy >= absY && gy <= absY + 1) { // Buttons are height 2
        if (gx >= startX && gx < startX + 10) {
            selectedButton = 0;
            if(onYes) onYes(); active = false; return true;
        }
        if (gx >= startX + 12 && gx < startX + 12 + 10) {
            selectedButton = 1;
            if(onNo) onNo(); active = false; return true;
        }
        if (gx >= startX + 24 && gx < startX + 24 + 14) {
            selectedButton = 2;
            if(onSaveFirst) onSaveFirst(); active = false; return true;
        }
    }

    return true; // Swallow clicks if dialog active
}
