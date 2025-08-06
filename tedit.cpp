/**
 * @file tedit_final.cpp
 * @brief C++ conversion of the TEE text editor, with all fixes and features implemented.
 *
 * This version includes a file browser for open/save, visual tweaks, and working menu hotkeys.
 *
 * Original Pascal Author: "Fritz/7271"
 * C++ Conversion and Refactoring: Gemini
 *
 * To compile:
 * g++ tedit_final.cpp -o tedit -lncursesw -std=c++17
 */

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <clocale>
#include <ncurses.h>
#include <string.h> // For strlen
#include <memory>   // For std::unique_ptr
#include <thread>   // For std::this_thread
#include <chrono>   // For sleep_for
#include <cctype>   // For tolower
#include <wchar.h>  // For cchar_t and wide-character functions
#include <map>      // For menu navigation system
#include <algorithm>// For std::swap, std::sort
#include <cstdio>   // For popen, pclose, fputs
#include <sstream>  // For std::stringstream

// --- New includes for File Browser ---
#include <dirent.h>   // For directory reading
#include <unistd.h>   // For getcwd, chdir
#include <sys/stat.h> // For stat() to check file types

// --- Key Code Defines for Ctrl + Arrow Keys ---
#define KEY_CTRL_LEFT 545
#define KEY_CTRL_RIGHT 560
#define KEY_CTRL_UP 566
#define KEY_CTRL_DOWN 525
#define KEY_SHIFT_CTRL_LEFT 546
#define KEY_SHIFT_CTRL_RIGHT 561
#define KEY_SHIFT_CTRL_UP 567
#define KEY_SHIFT_CTRL_DOWN 526
#define KEY_CTRL_W 23 // Ctrl+W for closing window


// --- Forward Declarations ---
class Renderer;
class TextEditor;
struct EditorBuffer;

// --- Data Structures ---

struct Line {
    std::string text;
    Line* prev = nullptr;
    Line* next = nullptr;
    bool selected = false;
    int selection_start_col = 0;
    int selection_end_col = 0;
};

// --- New struct for file browser ---
struct FileEntry {
    std::string name;
    bool is_directory;
};

enum MenuAction { CLOSE_MENU, ITEM_SELECTED, NAVIGATE_LEFT, NAVIGATE_RIGHT, RESIZE_OCCURRED };

struct SyntaxToken {
    std::string text;
    int colorId;
    int flags = 0; // For attributes like A_BOLD
};

// --- New struct for Undo/Redo System ---
struct UndoRecord {
    std::vector<std::string> lines;
    int cursor_line_num;
    int cursor_col;
    int first_visible_line_num;
};

// --- Structure to hold the state of a single file buffer ---
struct EditorBuffer {
    // Document Content
    Line *document_head = nullptr;
    int total_lines = 0;

    // File Info
    std::string filename;
    bool changed = false;
    bool is_new_file = false;

    // Editor State
    bool insert_mode = true;
    Line *current_line = nullptr;
    Line *first_visible_line = nullptr;
    int cursor_col = 1;
    int current_line_num = 1;
    int cursor_screen_y = 0;
    int horizontal_scroll_offset = 1;

    // Selection State
    bool selecting = false;
    Line* selection_anchor_line = nullptr;
    int selection_anchor_col = 1;
    int selection_anchor_linenum = 1;
    
    // Undo/Redo Stacks
    std::vector<UndoRecord> undo_stack;
    std::vector<UndoRecord> redo_stack;

    // Syntax Highlighting
    enum SyntaxType { NONE, C_CPP, MAKEFILE, CMAKE };
    SyntaxType syntax_type = NONE;
    std::map<std::string, int> keywords;

    // 1. Default constructor for a new, empty buffer
    EditorBuffer() {
        document_head = new Line();
        total_lines = 1;
        current_line = document_head;
        first_visible_line = document_head;
        filename = "new_file.txt";
        is_new_file = true;
    }

    // 2. Destructor to clean up the linked list of lines
    ~EditorBuffer() {
        Line* p = document_head;
        while (p != nullptr) {
            Line* q = p;
            p = p->next;
            delete q;
        }
    }

    // 3. Copy Constructor (deep copy)
    EditorBuffer(const EditorBuffer& other) : 
        total_lines(other.total_lines), filename(other.filename), changed(other.changed),
        is_new_file(other.is_new_file), insert_mode(other.insert_mode), 
        cursor_col(other.cursor_col), current_line_num(other.current_line_num),
        cursor_screen_y(other.cursor_screen_y), horizontal_scroll_offset(other.horizontal_scroll_offset),
        selecting(other.selecting), selection_anchor_col(other.selection_anchor_col),
        selection_anchor_linenum(other.selection_anchor_linenum), syntax_type(other.syntax_type),
        keywords(other.keywords)
    {
        if (!other.document_head) {
            document_head = nullptr;
            current_line = nullptr;
            first_visible_line = nullptr;
            selection_anchor_line = nullptr;
            return;
        }

        document_head = new Line{other.document_head->text};
        Line* this_curr = document_head;
        const Line* other_curr = other.document_head;

        if (other_curr == other.current_line) current_line = this_curr;
        if (other_curr == other.first_visible_line) first_visible_line = this_curr;
        if (other_curr == other.selection_anchor_line) selection_anchor_line = this_curr;

        other_curr = other_curr->next;
        while(other_curr) {
            this_curr->next = new Line{other_curr->text, this_curr};
            this_curr = this_curr->next;

            if (other_curr == other.current_line) current_line = this_curr;
            if (other_curr == other.first_visible_line) first_visible_line = this_curr;
            if (other_curr == other.selection_anchor_line) selection_anchor_line = this_curr;
            
            other_curr = other_curr->next;
        }
    }
    
    // 4. Copy Assignment Operator
    EditorBuffer& operator=(const EditorBuffer& other) {
        if (this == &other) return *this;
        
        EditorBuffer temp(other);
        std::swap(*this, temp);
        return *this;
    }

    // 5. Move Constructor
    EditorBuffer(EditorBuffer&& other) noexcept : 
        document_head(other.document_head), total_lines(other.total_lines),
        filename(std::move(other.filename)), changed(other.changed),
        is_new_file(other.is_new_file), insert_mode(other.insert_mode),
        current_line(other.current_line), first_visible_line(other.first_visible_line),
        cursor_col(other.cursor_col), current_line_num(other.current_line_num),
        cursor_screen_y(other.cursor_screen_y), horizontal_scroll_offset(other.horizontal_scroll_offset),
        selecting(other.selecting), selection_anchor_line(other.selection_anchor_line),
        selection_anchor_col(other.selection_anchor_col), selection_anchor_linenum(other.selection_anchor_linenum),
        undo_stack(std::move(other.undo_stack)), redo_stack(std::move(other.redo_stack)),
        syntax_type(other.syntax_type), keywords(std::move(other.keywords))
    {
        other.document_head = nullptr;
    }

    // 6. Move Assignment Operator
    EditorBuffer& operator=(EditorBuffer&& other) noexcept {
        if (this == &other) return *this;
        Line* p = document_head;
        while (p != nullptr) { Line* q = p; p = p->next; delete q; }
        document_head = other.document_head;
        total_lines = other.total_lines;
        filename = std::move(other.filename);
        changed = other.changed;
        is_new_file = other.is_new_file;
        insert_mode = other.insert_mode;
        current_line = other.current_line;
        first_visible_line = other.first_visible_line;
        cursor_col = other.cursor_col;
        current_line_num = other.current_line_num;
        cursor_screen_y = other.cursor_screen_y;
        horizontal_scroll_offset = other.horizontal_scroll_offset;
        selecting = other.selecting;
        selection_anchor_line = other.selection_anchor_line;
        selection_anchor_col = other.selection_anchor_col;
        selection_anchor_linenum = other.selection_anchor_linenum;
        undo_stack = std::move(other.undo_stack);
        redo_stack = std::move(other.redo_stack);
        syntax_type = other.syntax_type;
        keywords = std::move(other.keywords);
        other.document_head = nullptr;
        return *this;
    }
};


// --- UTF-8 Conversion Utility ---
std::string wchar_to_utf8(wchar_t wc) {
    std::string str;
    if (wc < 0x80) { str.push_back(static_cast<char>(wc)); } 
    else if (wc < 0x800) { str.push_back(static_cast<char>(0xC0 | (wc >> 6))); str.push_back(static_cast<char>(0x80 | (wc & 0x3F))); } 
    else if (wc < 0x10000) { str.push_back(static_cast<char>(0xE0 | (wc >> 12))); str.push_back(static_cast<char>(0x80 | ((wc >> 6) & 0x3F))); str.push_back(static_cast<char>(0x80 | (wc & 0x3F))); } 
    else if (wc < 0x110000) { str.push_back(static_cast<char>(0xF0 | (wc >> 18))); str.push_back(static_cast<char>(0x80 | ((wc >> 12) & 0x3F))); str.push_back(static_cast<char>(0x80 | ((wc >> 6) & 0x3F))); str.push_back(static_cast<char>(0x80 | (wc & 0x3F))); }
    return str;
}

bool ends_with(const std::string& str, const std::string& suffix) {
    if (str.length() < suffix.length()) { return false; }
    return str.rfind(suffix) == (str.length() - suffix.length());
}


// =================================================================================
// RENDERER CLASS
// =================================================================================

class Renderer {
public:
    enum ColorPairID {
        CP_DEFAULT_TEXT = 1, CP_HIGHLIGHT, CP_MENU_BAR, CP_MENU_ITEM,
        CP_MENU_SELECTED, CP_DIALOG, CP_DIALOG_BUTTON, CP_SELECTION,
        CP_STATUS_BAR, CP_STATUS_BAR_HIGHLIGHT, CP_SHADOW,
        CP_DIALOG_TITLE, CP_CHANGED_INDICATOR,
        CP_SYNTAX_KEYWORD, CP_SYNTAX_COMMENT, CP_SYNTAX_STRING,
        CP_SYNTAX_NUMBER, CP_SYNTAX_PREPROCESSOR
    };
    
    enum BoxStyle { SINGLE, DOUBLE };

    Renderer() {
        setlocale(LC_ALL, ""); initscr(); cbreak(); noecho();
        keypad(stdscr, TRUE); nodelay(stdscr, TRUE); curs_set(1);
        start_color(); use_default_colors();

        init_pair(CP_DEFAULT_TEXT, COLOR_WHITE, COLOR_BLUE);
        init_pair(CP_HIGHLIGHT, COLOR_BLACK, COLOR_CYAN);
        init_pair(CP_MENU_BAR, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_MENU_ITEM, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_MENU_SELECTED, COLOR_WHITE, COLOR_CYAN);
        init_pair(CP_DIALOG, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_DIALOG_BUTTON, COLOR_WHITE, COLOR_BLUE);
        init_pair(CP_SELECTION, COLOR_BLACK, COLOR_YELLOW);
        init_pair(CP_STATUS_BAR, COLOR_BLACK, COLOR_WHITE); 
        init_pair(CP_STATUS_BAR_HIGHLIGHT, COLOR_RED, COLOR_WHITE);   
        init_pair(CP_SHADOW, COLOR_WHITE, COLOR_BLACK);
        init_pair(CP_DIALOG_TITLE, COLOR_RED, COLOR_WHITE); 
        init_pair(CP_CHANGED_INDICATOR, COLOR_GREEN, COLOR_BLUE);
        init_pair(CP_SYNTAX_KEYWORD, COLOR_WHITE, COLOR_BLUE);
        init_pair(CP_SYNTAX_COMMENT, COLOR_GREEN, COLOR_BLUE);
        init_pair(CP_SYNTAX_STRING, COLOR_RED, COLOR_BLUE);
        init_pair(CP_SYNTAX_NUMBER, COLOR_RED, COLOR_BLUE);
        init_pair(CP_SYNTAX_PREPROCESSOR, COLOR_CYAN, COLOR_BLUE);

        getmaxyx(stdscr, m_height, m_width);
    }

    ~Renderer() { curs_set(1); endwin(); }
    void clear() { werase(stdscr); }
    void refresh() { wrefresh(stdscr); }
    void updateDimensions() { getmaxyx(stdscr, m_height, m_width); }

    void drawText(int x, int y, const std::string& text, int colorId, int flags = 0) {
        wattron(stdscr, COLOR_PAIR(colorId));
        if (flags & A_BOLD) wattron(stdscr, A_BOLD);
        if (flags & A_UNDERLINE) wattron(stdscr, A_UNDERLINE);
        mvwaddstr(stdscr, y, x, text.c_str());
        if (flags & A_UNDERLINE) wattroff(stdscr, A_UNDERLINE);
        if (flags & A_BOLD) wattroff(stdscr, A_BOLD);
        wattroff(stdscr, COLOR_PAIR(colorId));
    }

    void drawStyledText(int x, int y, const std::string& text, int colorId) {
        wattron(stdscr, COLOR_PAIR(colorId));
        wmove(stdscr, y, x);
        for (size_t i = 0; i < text.length(); ++i) {
            if (text[i] == '&' && i + 1 < text.length()) {
                i++;
                wattron(stdscr, A_UNDERLINE);
                waddch(stdscr, text[i]);
                wattroff(stdscr, A_UNDERLINE);
            } else { waddch(stdscr, text[i]); }
        }
        wattroff(stdscr, COLOR_PAIR(colorId));
    }
    
    void drawBox(int x, int y, int w, int h, int colorId, BoxStyle style = SINGLE) {
        if (w < 2 || h < 2) return; 
        if (style == SINGLE) {
            wattron(stdscr, COLOR_PAIR(colorId));
            mvaddch(y, x, ACS_ULCORNER); mvaddch(y, x + w - 1, ACS_URCORNER);
            mvaddch(y + h - 1, x, ACS_LLCORNER); mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
            mvhline(y, x + 1, ACS_HLINE, w - 2); mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
            mvvline(y + 1, x, ACS_VLINE, h - 2); mvvline(y + 1, x + w - 1, ACS_VLINE, h - 2);
            wattroff(stdscr, COLOR_PAIR(colorId));
        } else {
            cchar_t tl, tr, bl, br, hline, vline;
            setcchar(&tl, L"╔", WA_NORMAL, colorId, NULL); setcchar(&tr, L"╗", WA_NORMAL, colorId, NULL);
            setcchar(&bl, L"╚", WA_NORMAL, colorId, NULL); setcchar(&br, L"╝", WA_NORMAL, colorId, NULL);
            setcchar(&hline, L"═", WA_NORMAL, colorId, NULL); setcchar(&vline, L"║", WA_NORMAL, colorId, NULL);
            mvwadd_wch(stdscr, y, x, &tl); mvwadd_wch(stdscr, y, x + w - 1, &tr);
            mvwadd_wch(stdscr, y + h - 1, x, &bl); mvwadd_wch(stdscr, y + h - 1, x + w - 1, &br);
            mvwhline_set(stdscr, y, x + 1, &hline, w - 2); mvwhline_set(stdscr, y + h - 1, x + 1, &hline, w - 2);
            mvwvline_set(stdscr, y + 1, x, &vline, h - 2); mvwvline_set(stdscr, y + 1, x + w - 1, &vline, h - 2);
        }
    }
    
    void drawBoxWithTitle(int x, int y, int w, int h, int colorId, BoxStyle style, const std::string& title, int title_color, int title_flags) {
        drawBox(x, y, w, h, colorId, style);
        if (!title.empty()) {
            std::string spaced_title = " " + title + " ";
            if (spaced_title.length() < (size_t)w - 2) {
               drawText(x + (w - spaced_title.length()) / 2, y, spaced_title, title_color, title_flags);
            }
        }
    }

    void drawShadow(int x, int y, int w, int h) {
        cchar_t underlying_char, shadow_char;
        wchar_t char_buffer[2] = {0};
        for (int row = y + 1; row < y + h + 1; ++row) {
            if (row < m_height && (x + w) < m_width) {
                mvin_wch(row, x + w, &underlying_char);
                char_buffer[0] = underlying_char.chars[0]; if (char_buffer[0] == 0) { char_buffer[0] = L' '; }
                setcchar(&shadow_char, char_buffer, A_NORMAL, CP_SHADOW, NULL); mvadd_wch(row, x + w, &shadow_char);
            }
        }
        for (int col = x + 1; col < x + w + 1; ++col) {
            if ((y + h) < m_height && col < m_width) {
                mvin_wch(y + h, col, &underlying_char);
                char_buffer[0] = underlying_char.chars[0]; if (char_buffer[0] == 0) { char_buffer[0] = L' '; }
                setcchar(&shadow_char, char_buffer, A_NORMAL, CP_SHADOW, NULL); mvadd_wch(y + h, col, &shadow_char);
            }
        }
    }

    wint_t getChar() { wint_t ch; int result = wget_wch(stdscr, &ch); return (result == ERR) ? ERR : ch; }
    void hideCursor() { curs_set(0); }
    void showCursor() { curs_set(1); }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    void setCursor(int x, int y) { move(y, x); }

private:
    int m_width = 0, m_height = 0;
};

// =================================================================================
// TEXT EDITOR CLASS
// =================================================================================

class TextEditor {
private:
    // --- Multi-buffer state ---
    std::vector<EditorBuffer> m_buffers;
    int m_current_buffer_idx = -1;

    // --- Global state ---
    std::unique_ptr<Renderer> m_renderer;
    std::vector<std::string> m_clipboard;

    // UI Coordinates
    int m_text_area_start_x = 1, m_text_area_start_y = 2, m_text_area_end_x = 0, m_text_area_end_y = 0;

    // --- Menus ---
    const std::vector<std::string> m_menus = {" &File " , " &Edit " , " &Search ", " &View "  , " &Build " , " &Window ", " &Options ", " &Help "  };
    const std::vector<int> m_menu_positions = {2        , 9         , 17        , 26       , 33       , 41         , 49       , 58       };
    
    const std::vector<std::string> m_submenu_file = {" &New           Ctrl+N", " &Open...       Ctrl+O", " -------------- ", " &Save          Ctrl+S", " Save &As...    ", " -------------- ", " E&xit          Alt+X"};
    const std::vector<std::string> m_submenu_edit = {" &Undo       Alt+BckSp", " &Redo           Alt+Y", " -------------- ", " Cu&t           Ctrl+X", " &Copy          Ctrl+C", " &Paste         Ctrl+V", " &Delete        ", " -------------- ", " Comment Line   ", " Uncomment Line "};
    const std::vector<std::string> m_submenu_search = {" &Find...       ", " Find &Next      ", " Find Pre&vious ", " &Replace...    ", " -------------- ", " &Go To Line... "};
    const std::vector<std::string> m_submenu_view = {" Line &Numbers  ", " &Syntax Hi-Lite "};
    const std::vector<std::string> m_submenu_build = {" &Compile       ", " &Build         ", " &Run           "};
    const std::vector<std::string> m_submenu_window = {" &Next Window    F6", "&Previous Window S-F6", "&Close Window  Alt+W"};
    const std::vector<std::string> m_submenu_options = {" Editor &Settings... "};
    const std::vector<std::string> m_submenu_help = {" &View Help...  ", " &About...      "};

public:
    void run(int argc, char* argv[]);

private:
    EditorBuffer& currentBuffer() { return m_buffers[m_current_buffer_idx]; }
    
    void drawEditorState(int active_menu_id = -1);
    void drawMainUI();
    void drawTextArea();
    void drawMenuBar(int active_menu_id = -1);
    void drawStatusBar();
    void drawScrollbars();
    int msgwin_yesno(const std::string& s);
    void msgwin(const std::string& s);
    void read_file(EditorBuffer& buffer);
    void write_file(EditorBuffer& buffer);
    void main_loop();
    void insert_line_after(EditorBuffer& buffer, Line* current_p, const std::string& s);
    void process_key(wint_t ch);
    void handle_alt_key(wint_t key);
    void update_cursor_and_scroll();
    void handleResize(); 
    void ClearSelection();
    void UpdateSelection();
    void DeleteSelection();
    void HandleCopy();
    void HandleCut();
    void HandlePaste();
    void CreateUndoPoint(EditorBuffer& buffer);
    void HandleUndo();
    void HandleRedo();
    void RestoreStateFromRecord(EditorBuffer& buffer, const UndoRecord& record);
    void GoToNextWord();
    void GoToPreviousWord();
    void GoToNextParagraph();
    void GoToPreviousParagraph();
    void ActivateMenuBar(int initial_menu_id);
    MenuAction CallSubMenu(const std::vector<std::string>& menuItems, int x, int y, int menu_id);
    void DoNew();
    void selectfile();
    void OpenFileBrowser(); 
    void SaveFileBrowser(); 
    void NextWindow();
    void PreviousWindow();
    void CloseWindow();
    void SwitchToBuffer(int index);
    
    void setSyntaxType(EditorBuffer& buffer);
    void loadKeywords(EditorBuffer& buffer);
    std::vector<SyntaxToken> parseLine(const std::string& line);
    void drawHighlightedLine(const std::vector<SyntaxToken>& tokens, int screen_y);
    void noti() { msgwin("Not Implemented yet."); }
    void about_box() { msgwin("TEE C++ Editor - by Gemini"); }
};

// --- Method Implementations ---

void TextEditor::run(int argc, char* argv[]) {
    m_renderer = std::make_unique<Renderer>();
    if (argc < 2) {
        DoNew();
    } else {
        m_buffers.emplace_back();
        m_current_buffer_idx = 0;
        currentBuffer().filename = argv[1];
        read_file(currentBuffer());
    }
    
    m_text_area_start_x = 1; 
    m_text_area_start_y = 2;
    m_text_area_end_x = m_renderer->getWidth() - 3; 
    m_text_area_end_y = m_renderer->getHeight() - 4;
    currentBuffer().cursor_screen_y = m_text_area_start_y;

    main_loop();
}

void TextEditor::read_file(EditorBuffer& buffer) {
    // Clear existing content
    Line* p = buffer.document_head;
    while (p != nullptr) { Line* q = p; p = p->next; delete q; }
    buffer.document_head = nullptr;
    buffer.total_lines = 0;

    std::ifstream f(buffer.filename);
    Line* current = nullptr;
    std::string line_str;
    if (!f.is_open()) {
        buffer.document_head = new Line();
        buffer.total_lines = 1;
    } else {
        buffer.is_new_file = false;
        while (getline(f, line_str)) {
            buffer.total_lines++;
            if (!line_str.empty() && line_str.back() == '\r') { line_str.pop_back(); }
            Line* new_line = new Line();
            new_line->text = line_str;
            if (buffer.document_head == nullptr) { buffer.document_head = current = new_line; } 
            else { current->next = new_line; new_line->prev = current; current = new_line; }
        }
        f.close();
    }
    if (buffer.document_head == nullptr) { 
        buffer.document_head = new Line();
        buffer.total_lines = 1;
    }
    buffer.current_line = buffer.first_visible_line = buffer.document_head;
    buffer.current_line_num = 1; buffer.cursor_col = 1; buffer.cursor_screen_y = m_text_area_start_y; buffer.changed = false;
    setSyntaxType(buffer);
}

void TextEditor::write_file(EditorBuffer& buffer) {
    std::ofstream f(buffer.filename);
    if (!f.is_open()) { msgwin("Error: Cannot write to file " + buffer.filename); return; }
    for (Line* p = buffer.document_head; p != nullptr; p = p->next) { f << p->text << std::endl; }
    f.close();
    buffer.changed = false;
    buffer.is_new_file = false;
}

void TextEditor::insert_line_after(EditorBuffer& buffer, Line* current_p, const std::string& s) {
    if (!current_p) return;
    Line* p = new Line();
    p->text = s;
    p->next = current_p->next;
    p->prev = current_p;
    if (current_p->next) { current_p->next->prev = p; }
    current_p->next = p;
    buffer.changed = true;
    buffer.total_lines++;
}

void TextEditor::drawMainUI() {
    wbkgd(stdscr, COLOR_PAIR(Renderer::CP_DEFAULT_TEXT));
    
    int box_x = m_text_area_start_x - 1;
    int box_y = m_text_area_start_y - 1;
    int box_w = m_text_area_end_x - m_text_area_start_x + 3;
    int box_h = m_text_area_end_y - m_text_area_start_y + 3;
    
    m_renderer->drawBox(box_x, box_y, box_w, box_h, Renderer::CP_DEFAULT_TEXT, Renderer::BoxStyle::DOUBLE);

    if (m_current_buffer_idx != -1) {
        std::string filename_part = " " + currentBuffer().filename + " ";
        std::string indicator_part = "* ";
        for (char& c : filename_part) c = toupper(c);

        int total_len = filename_part.length() + (currentBuffer().changed ? indicator_part.length() : 0);
        int title_x = box_x + (box_w - total_len) / 2;
        
        int current_x = title_x;
        if (currentBuffer().changed) {
            m_renderer->drawText(current_x, box_y, indicator_part, Renderer::CP_CHANGED_INDICATOR, A_BOLD);
            current_x += indicator_part.length();
        }
        m_renderer->drawText(current_x, box_y, filename_part, Renderer::CP_HIGHLIGHT);
    }
}

void TextEditor::drawTextArea() {
    if (m_current_buffer_idx == -1) return;
    EditorBuffer& buffer = currentBuffer();

    Line* p = buffer.first_visible_line;
    int text_area_height = m_text_area_end_y - m_text_area_start_y + 1;
    int text_area_width = m_text_area_end_x - m_text_area_start_x + 1;
    if (text_area_height <= 0 || text_area_width <= 0) return;

    for(int i = 0; i < text_area_height; ++i) {
        int current_line_y = m_text_area_start_y + i;
        m_renderer->drawText(m_text_area_start_x, current_line_y, std::string(text_area_width, ' '), Renderer::CP_DEFAULT_TEXT);
        if (p != nullptr) {
            if (p->selected) {
                int screen_x = m_text_area_start_x;
                for (size_t char_idx = 0; char_idx < p->text.length(); ++char_idx) {
                    int current_col = char_idx + 1;
                    if (current_col >= buffer.horizontal_scroll_offset) {
                        if (screen_x > m_text_area_end_x) break;
                        bool is_char_selected = (current_col >= p->selection_start_col && current_col < p->selection_end_col);
                        int color = is_char_selected ? Renderer::CP_SELECTION : Renderer::CP_DEFAULT_TEXT;
                        m_renderer->drawText(screen_x, current_line_y, std::string(1, p->text[char_idx]), color);
                        screen_x++;
                    }
                }
            } else if (buffer.syntax_type != EditorBuffer::NONE) {
                auto tokens = parseLine(p->text);
                drawHighlightedLine(tokens, current_line_y);
            } else {
                std::string line_to_draw;
                if (buffer.horizontal_scroll_offset <= (int)p->text.length() + 1) {
                    std::string sub = p->text.substr(buffer.horizontal_scroll_offset - 1);
                    if (sub.length() > (size_t)text_area_width) { sub = sub.substr(0, text_area_width); }
                    line_to_draw = sub;
                }
                m_renderer->drawText(m_text_area_start_x, current_line_y, line_to_draw, Renderer::CP_DEFAULT_TEXT);
            }
            p = p->next;
        }
    }
}

void TextEditor::drawMenuBar(int active_menu_id) {
    int w = m_renderer->getWidth();
    m_renderer->drawText(0, 0, std::string(w, ' '), Renderer::CP_MENU_BAR);
    for (size_t i = 0; i < m_menus.size(); ++i) {
        if (m_menu_positions[i] + m_menus[i].length() > (size_t)w) continue;
        int menu_id = i + 1;
        bool is_active = (menu_id == active_menu_id);
        int bar_color = is_active ? Renderer::CP_MENU_SELECTED : Renderer::CP_MENU_BAR;
        m_renderer->drawStyledText(m_menu_positions[i], 0, m_menus[i], bar_color);
    }
}

void TextEditor::drawStatusBar() {
    int w = m_renderer->getWidth();
    int h = m_renderer->getHeight();
    if (h <= 0 || w <= 0) return;

    m_renderer->drawText(0, h - 1, std::string(w, ' '), Renderer::CP_STATUS_BAR);

    if (w > 50) {
        m_renderer->drawText(1, h - 1, "F1", Renderer::CP_STATUS_BAR_HIGHLIGHT);
        m_renderer->drawText(4, h - 1, "Help", Renderer::CP_STATUS_BAR);
        m_renderer->drawText(10, h - 1, "F2", Renderer::CP_STATUS_BAR_HIGHLIGHT);
        m_renderer->drawText(13, h - 1, "Save", Renderer::CP_STATUS_BAR);
        m_renderer->drawText(19, h - 1, "F3", Renderer::CP_STATUS_BAR_HIGHLIGHT);
        m_renderer->drawText(22, h - 1, "Open", Renderer::CP_STATUS_BAR);
        m_renderer->drawText(28, h - 1, "F10", Renderer::CP_STATUS_BAR_HIGHLIGHT);
        m_renderer->drawText(32, h - 1, "Menu", Renderer::CP_STATUS_BAR);
        m_renderer->drawText(38, h - 1, "Alt-X", Renderer::CP_STATUS_BAR_HIGHLIGHT);
        m_renderer->drawText(44, h - 1, "Exit", Renderer::CP_STATUS_BAR);
    }
    
    if (m_current_buffer_idx != -1) {
        EditorBuffer& buffer = currentBuffer();
        char status_buf[120];
        snprintf(status_buf, sizeof(status_buf), "Line: %-5d Col: %-5d %s", buffer.current_line_num, buffer.cursor_col, (buffer.insert_mode ? "INS" : "OVR"));
        if (w > 50 + (int)strlen(status_buf)) {
            m_renderer->drawText(w - strlen(status_buf) - 2, h - 1, status_buf, Renderer::CP_STATUS_BAR);
        }
    }
}

void TextEditor::drawScrollbars() {
    if (m_renderer->getWidth() < 5 || m_renderer->getHeight() < 5 || m_current_buffer_idx == -1) return;
    EditorBuffer& buffer = currentBuffer();

    int page_height = m_text_area_end_y - m_text_area_start_y + 1;
    int bar_x = m_text_area_end_x + 2;
    int bar_y = m_text_area_end_y + 2;
    
    int first_visible_linenum = 1;
    Line* p = buffer.document_head;
    while(p && p != buffer.first_visible_line) { first_visible_linenum++; p = p->next; }

    attron(COLOR_PAIR(Renderer::CP_HIGHLIGHT));
    mvaddch(m_text_area_start_y - 1, bar_x, ACS_UARROW);
    mvaddch(m_text_area_end_y + 1, bar_x, ACS_DARROW);
    
    int track_height = page_height;
    if (track_height > 0) {
        for(int i = 0; i < track_height; ++i) { mvaddch(m_text_area_start_y + i, bar_x, ACS_CKBOARD); }
        if (buffer.total_lines > page_height) {
            float proportion_scrolled = (buffer.total_lines > 1) ? (float)(first_visible_linenum - 1) / (buffer.total_lines - page_height) : 0.0f;
            if (proportion_scrolled > 1.0) proportion_scrolled = 1.0;
            int thumb_y = m_text_area_start_y + (int)((track_height - 1) * proportion_scrolled);
            mvaddch(thumb_y, bar_x, ACS_BLOCK);
        } else { mvaddch(m_text_area_start_y, bar_x, ACS_BLOCK); }
    }
    attroff(COLOR_PAIR(Renderer::CP_HIGHLIGHT));

    int page_width = m_text_area_end_x - m_text_area_start_x + 1;
    int line_width = buffer.current_line ? buffer.current_line->text.length() : 0;

    attron(COLOR_PAIR(Renderer::CP_HIGHLIGHT));
    mvaddch(bar_y, m_text_area_start_x - 1, ACS_LARROW);
    mvaddch(bar_y, m_text_area_end_x + 1, ACS_RARROW);
    int track_width = page_width;

    if (track_width > 0) {
        for(int i = 0; i < track_width; ++i) { mvaddch(bar_y, m_text_area_start_x + i, ACS_CKBOARD); }
        if (line_width > page_width) {
            float scrollable_width = line_width - page_width; if (scrollable_width == 0) scrollable_width = 1;
            float proportion_scrolled_h = (float)(buffer.horizontal_scroll_offset - 1) / scrollable_width;
            if (proportion_scrolled_h > 1.0) proportion_scrolled_h = 1.0;
            int thumb_x = m_text_area_start_x + (int)((track_width - 1) * proportion_scrolled_h);
            mvaddch(bar_y, thumb_x, ACS_BLOCK);
        } else { mvaddch(bar_y, m_text_area_start_x, ACS_BLOCK); }
    }
    attroff(COLOR_PAIR(Renderer::CP_HIGHLIGHT));
}


void TextEditor::drawEditorState(int active_menu_id) {
    m_renderer->clear();
    drawMainUI();
    drawTextArea();
    drawMenuBar(active_menu_id);
    drawStatusBar();
    drawScrollbars();
}

int TextEditor::msgwin_yesno(const std::string& s) {
    int h = 8, w = 42;
    int starty = (m_renderer->getHeight() - h) / 2;
    int startx = (m_renderer->getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);
    
    m_renderer->drawShadow(startx, starty, w, h);
    m_renderer->drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::SINGLE, " Question ", Renderer::CP_DIALOG_BUTTON, 0);
    m_renderer->drawText(startx + 2, starty + 2, s, Renderer::CP_DIALOG);
    
    int selection = 0; // 0 for Yes, 1 for No
    nodelay(stdscr, FALSE);
    while(true) {
        m_renderer->drawText(startx + 8, starty + 5, " Yes ", selection == 0 ? Renderer::CP_DIALOG_BUTTON : Renderer::CP_DIALOG);
        m_renderer->drawText(startx + w - 13, starty + 5, " No ", selection == 1 ? Renderer::CP_DIALOG_BUTTON : Renderer::CP_DIALOG);
        m_renderer->refresh();
        wint_t ch = m_renderer->getChar();
        if (ch == KEY_LEFT || ch == KEY_RIGHT) {
            selection = 1 - selection;
        } else if (ch == KEY_ENTER || ch == 10 || ch == 13) {
            copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE); delwin(behind);
            nodelay(stdscr, TRUE);
            return selection == 0 ? 1 : 0;
        } else if (ch == 27) { // ESC
            copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE); delwin(behind);
            nodelay(stdscr, TRUE);
            return -1; // Cancel
        }
    }
}


void TextEditor::msgwin(const std::string& s) {
    int h = 8, w = 42;
    int starty = (m_renderer->getHeight() - h) / 2;
    int startx = (m_renderer->getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);
    
    wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
    for (int i = 1; i < h - 1; ++i) { mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str()); }
    wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

    m_renderer->drawShadow(startx, starty, w, h);
    m_renderer->drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::SINGLE, " Message ", Renderer::CP_DIALOG_BUTTON, 0);
    m_renderer->drawText(startx + 2, starty + 3, s, Renderer::CP_DIALOG);
    m_renderer->drawText(startx + (w - 6) / 2, starty + 5, " Okey ", Renderer::CP_DIALOG_BUTTON);
    m_renderer->refresh();

    nodelay(stdscr, FALSE);
    wint_t ch;
    do { ch = m_renderer->getChar(); } while (ch != KEY_ENTER && ch != 10 && ch != 13 && ch != 27);
    nodelay(stdscr, TRUE);
    
    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE);
    delwin(behind);
}

bool main_loop_running = true;

void TextEditor::handleResize() {
    clearok(stdscr, TRUE); clear();
    m_renderer->updateDimensions();
    m_text_area_end_x = m_renderer->getWidth() - 3;
    m_text_area_end_y = m_renderer->getHeight() - 4;
    if (m_text_area_end_x <= m_text_area_start_x) { m_text_area_end_x = m_text_area_start_x + 1; }
    if (m_text_area_end_y <= m_text_area_start_y) { m_text_area_end_y = m_text_area_start_y + 1; }
    update_cursor_and_scroll();
}


void TextEditor::main_loop() {
    main_loop_running = true;
    while (main_loop_running) {
        update_cursor_and_scroll();
        drawEditorState(); 
        if (m_current_buffer_idx != -1) {
            EditorBuffer& buffer = currentBuffer();
            m_renderer->setCursor(buffer.cursor_col - buffer.horizontal_scroll_offset + m_text_area_start_x, buffer.cursor_screen_y);
        }
        m_renderer->refresh();

        wint_t ch = m_renderer->getChar();

        if (ch == KEY_RESIZE) { handleResize(); continue; }
        
        if (ch != ERR) {
            if (ch == 27 || ch == KEY_F(10)) { process_key(ch); } 
            else {
                std::vector<wint_t> input_buffer; input_buffer.push_back(ch);
                nodelay(stdscr, TRUE); timeout(1);
                wint_t next_ch;
                while ((next_ch = m_renderer->getChar()) != ERR) { input_buffer.push_back(next_ch); }
                timeout(-1); nodelay(stdscr, TRUE);
                for (wint_t key_press : input_buffer) { process_key(key_press); }
            }
        } else { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    }
}

void TextEditor::update_cursor_and_scroll() {
    if (m_current_buffer_idx == -1) return;
    EditorBuffer& buffer = currentBuffer();

    if (!buffer.current_line) return;
    if (buffer.cursor_col > (int)buffer.current_line->text.length() + 1) { buffer.cursor_col = buffer.current_line->text.length() + 1; }
    if (buffer.cursor_screen_y < m_text_area_start_y) { buffer.first_visible_line = buffer.current_line; buffer.cursor_screen_y = m_text_area_start_y; }
    if (buffer.cursor_screen_y > m_text_area_end_y) {
        buffer.cursor_screen_y = m_text_area_end_y;
        Line* p = buffer.current_line;
        for(int i = 0; i < (m_text_area_end_y - m_text_area_start_y) && p->prev; ++i) { p = p->prev; }
        buffer.first_visible_line = p;
    }
    int text_area_width = m_text_area_end_x - m_text_area_start_x;
    if (buffer.cursor_col < buffer.horizontal_scroll_offset) { buffer.horizontal_scroll_offset = 1; }
    if (text_area_width > 0 && buffer.cursor_col >= buffer.horizontal_scroll_offset + text_area_width) { buffer.horizontal_scroll_offset = buffer.cursor_col - text_area_width; }
}

void TextEditor::handle_alt_key(wint_t key) {
    switch (tolower(key)) {
        case 'f': ActivateMenuBar(1); break;
        case 'e': ActivateMenuBar(2); break;
        case 's': ActivateMenuBar(3); break;
        case 'v': ActivateMenuBar(4); break;
        case 'b': ActivateMenuBar(5); break;
        case 'w': ActivateMenuBar(6); break;
        case 'o': ActivateMenuBar(7); break;
        case 'h': ActivateMenuBar(8); break;
        case 'x': main_loop_running = false; break;
        case 'y': HandleRedo(); break;
        case KEY_BACKSPACE: HandleUndo(); break;
        case 'c': CloseWindow(); break;
        case '1': if (m_buffers.size() >= 1) SwitchToBuffer(0); break;
        case '2': if (m_buffers.size() >= 2) SwitchToBuffer(1); break;
        case '3': if (m_buffers.size() >= 3) SwitchToBuffer(2); break;
        case '4': if (m_buffers.size() >= 4) SwitchToBuffer(3); break;
        case '5': if (m_buffers.size() >= 5) SwitchToBuffer(4); break;
        case '6': if (m_buffers.size() >= 6) SwitchToBuffer(5); break;
        case '7': if (m_buffers.size() >= 7) SwitchToBuffer(6); break;
        case '8': if (m_buffers.size() >= 8) SwitchToBuffer(7); break;
        case '9': if (m_buffers.size() >= 9) SwitchToBuffer(8); break;
        case '0': if (m_buffers.size() >= 10) SwitchToBuffer(9); break;
    }
}

void TextEditor::ClearSelection() {
    if (m_current_buffer_idx == -1) return;
    EditorBuffer& buffer = currentBuffer();
    if (!buffer.selecting && !buffer.selection_anchor_line) return;
    for(Line* p = buffer.document_head; p != nullptr; p = p->next) {
        p->selected = false;
        p->selection_start_col = 0;
        p->selection_end_col = 0;
    }
    buffer.selecting = false;
    buffer.selection_anchor_line = nullptr;
}

void TextEditor::UpdateSelection() {
    if (m_current_buffer_idx == -1) return;
    EditorBuffer& buffer = currentBuffer();

    for(Line* p = buffer.document_head; p != nullptr; p = p->next) { p->selected = false; }
    if (!buffer.selecting) return;
    Line* p_start = buffer.selection_anchor_line;
    int p_start_col = buffer.selection_anchor_col;
    int p_start_linenum = buffer.selection_anchor_linenum;
    Line* p_end = buffer.current_line;
    int p_end_col = buffer.cursor_col;
    int p_end_linenum = buffer.current_line_num;

    if (p_start_linenum > p_end_linenum || (p_start_linenum == p_end_linenum && p_start_col > p_end_col)) {
        std::swap(p_start, p_end);
        std::swap(p_start_col, p_end_col);
    }

    for (Line* p = p_start; p != nullptr; p = p->next) {
        p->selected = true;
        p->selection_start_col = (p == p_start) ? p_start_col : 1;
        p->selection_end_col = (p == p_end) ? p_end_col : p->text.length() + 1;
        if (p == p_end) break;
    }
}

void TextEditor::DeleteSelection() {
    if (m_current_buffer_idx == -1 || !currentBuffer().selecting) return;
    CreateUndoPoint(currentBuffer());
    EditorBuffer& buffer = currentBuffer();
    int lines_deleted_count = 0;
    Line* p_start = buffer.selection_anchor_line;
    int p_start_col = buffer.selection_anchor_col;
    int p_start_linenum = buffer.selection_anchor_linenum;
    Line* p_end = buffer.current_line;
    int p_end_col = buffer.cursor_col;
    int p_end_linenum = buffer.current_line_num;

    if (p_start_linenum > p_end_linenum || (p_start_linenum == p_end_linenum && p_start_col > p_end_col)) {
        std::swap(p_start, p_end);
        std::swap(p_start_col, p_end_col);
        std::swap(p_start_linenum, p_end_linenum);
    }

    buffer.current_line = p_start;
    buffer.cursor_col = p_start_col;
    buffer.current_line_num = p_start_linenum;
    
    Line* p = buffer.first_visible_line;
    int line_offset = 0;
    bool found = false;
    while(p) {
        if (p == buffer.current_line) { found = true; break; }
        if (p->next == nullptr) break;
        p = p->next; line_offset++;
    }

    if (found && m_text_area_start_y + line_offset <= m_text_area_end_y) {
        buffer.cursor_screen_y = m_text_area_start_y + line_offset;
    } else {
        buffer.first_visible_line = buffer.current_line;
        buffer.cursor_screen_y = m_text_area_start_y;
    }

    if (p_start == p_end) {
        p_start->text.erase(p_start_col - 1, p_end_col - p_start_col);
    } else {
        p_start->text.erase(p_start_col - 1);
        p_end->text.erase(0, p_end_col - 1);
        p_start->text += p_end->text;
        Line* current = p_start->next;
        while(current && current != p_end) {
            Line* to_delete = current;
            current = current->next;
            delete to_delete;
            lines_deleted_count++;
        }
        p_start->next = p_end->next;
        if (p_end->next) { p_end->next->prev = p_start; }
        delete p_end;
        lines_deleted_count++;
    }
    buffer.total_lines -= lines_deleted_count;
    buffer.changed = true;
    ClearSelection();
}

void TextEditor::HandleCopy() {
    if (m_current_buffer_idx == -1 || !currentBuffer().selecting) return;
    EditorBuffer& buffer = currentBuffer();
    m_clipboard.clear();
    
    Line* p_start = buffer.selection_anchor_line;
    int p_start_col = buffer.selection_anchor_col;
    int p_start_linenum = buffer.selection_anchor_linenum;
    Line* p_end = buffer.current_line;
    int p_end_col = buffer.cursor_col;
    int p_end_linenum = buffer.current_line_num;

    if (p_start_linenum > p_end_linenum || (p_start_linenum == p_end_linenum && p_start_col > p_end_col)) {
        std::swap(p_start, p_end);
        std::swap(p_start_col, p_end_col);
    }

    std::string text_to_copy;
    Line* p = p_start;
    while(p) {
        int start = (p == p_start) ? p_start_col : 1;
        int end = (p == p_end) ? p_end_col : p->text.length() + 1;
        std::string line_part = p->text.substr(start - 1, end - start);
        m_clipboard.push_back(line_part);
        text_to_copy += line_part;
        if (p == p_end) break;
        text_to_copy += "\n";
        p = p->next;
    }
    
    FILE* pipe = popen("xclip -selection clipboard -i", "w");
    if (pipe) { fputs(text_to_copy.c_str(), pipe); pclose(pipe); }
}

void TextEditor::HandleCut() {
    if (m_current_buffer_idx == -1 || !currentBuffer().selecting) return;
    HandleCopy();
    DeleteSelection();
}

void TextEditor::HandlePaste() {
    if (m_current_buffer_idx == -1) return;
    CreateUndoPoint(currentBuffer());
    EditorBuffer& buffer = currentBuffer();
    std::string pasted_text;
    FILE* pipe = popen("xclip -selection clipboard -o", "r");
    if (pipe) {
        char buf[128];
        while (fgets(buf, sizeof(buf), pipe) != nullptr) { pasted_text += buf; }
        pclose(pipe);
    }

    if (pasted_text.empty()) return;

    m_clipboard.clear();
    std::string current_line_str;
    std::stringstream ss(pasted_text);
    while (std::getline(ss, current_line_str, '\n')) { m_clipboard.push_back(current_line_str); }

    if (buffer.selecting) { DeleteSelection(); }
    
    std::string remainder = buffer.current_line->text.substr(buffer.cursor_col - 1);
    buffer.current_line->text.erase(buffer.cursor_col - 1);
    buffer.current_line->text += m_clipboard.front();
    Line* last_line = buffer.current_line;
    if (m_clipboard.size() > 1) {
        for (size_t i = 1; i < m_clipboard.size(); ++i) {
            std::string line_to_insert = m_clipboard[i];
            if (i == m_clipboard.size() - 1) {
                buffer.cursor_col = line_to_insert.length() + 1;
                line_to_insert += remainder;
            }
            insert_line_after(buffer, last_line, line_to_insert);
            last_line = last_line->next;
            buffer.current_line_num++;
            buffer.cursor_screen_y++;
        }
        buffer.current_line = last_line;
    } else {
        buffer.cursor_col = buffer.current_line->text.length() - remainder.length() + 1;
        buffer.current_line->text += remainder;
    }
    buffer.changed = true;
}

void TextEditor::CreateUndoPoint(EditorBuffer& buffer) {
    UndoRecord record;
    for(Line* p = buffer.document_head; p != nullptr; p = p->next) {
        record.lines.push_back(p->text);
    }
    record.cursor_line_num = buffer.current_line_num;
    record.cursor_col = buffer.cursor_col;
    
    int fv_linenum = 1;
    Line* p = buffer.document_head;
    while (p && p != buffer.first_visible_line) {
        p = p->next;
        fv_linenum++;
    }
    record.first_visible_line_num = fv_linenum;


    buffer.undo_stack.push_back(record);
    if (buffer.undo_stack.size() > 100) { // Limit undo history
        buffer.undo_stack.erase(buffer.undo_stack.begin());
    }
    buffer.redo_stack.clear(); // New action clears redo stack
}

void TextEditor::HandleUndo() {
    if (m_current_buffer_idx == -1 || currentBuffer().undo_stack.empty()) return;
    
    EditorBuffer& buffer = currentBuffer();
    
    // Save current state for redo
    UndoRecord redo_record;
    for(Line* p = buffer.document_head; p != nullptr; p = p->next) {
        redo_record.lines.push_back(p->text);
    }
    redo_record.cursor_line_num = buffer.current_line_num;
    redo_record.cursor_col = buffer.cursor_col;
    int fv_linenum = 1;
    Line* p = buffer.document_head;
    while (p && p != buffer.first_visible_line) {
        p = p->next;
        fv_linenum++;
    }
    redo_record.first_visible_line_num = fv_linenum;
    buffer.redo_stack.push_back(redo_record);

    // Pop and restore from undo stack
    UndoRecord undo_record = buffer.undo_stack.back();
    buffer.undo_stack.pop_back();
    RestoreStateFromRecord(buffer, undo_record);
}

void TextEditor::HandleRedo() {
    if (m_current_buffer_idx == -1 || currentBuffer().redo_stack.empty()) return;
    
    EditorBuffer& buffer = currentBuffer();

    // Save current state for undo
    UndoRecord undo_record;
    for(Line* p = buffer.document_head; p != nullptr; p = p->next) {
        undo_record.lines.push_back(p->text);
    }
    undo_record.cursor_line_num = buffer.current_line_num;
    undo_record.cursor_col = buffer.cursor_col;
    int fv_linenum = 1;
    Line* p = buffer.document_head;
    while (p && p != buffer.first_visible_line) {
        p = p->next;
        fv_linenum++;
    }
    undo_record.first_visible_line_num = fv_linenum;
    buffer.undo_stack.push_back(undo_record);

    // Pop and restore from redo stack
    UndoRecord redo_record = buffer.redo_stack.back();
    buffer.redo_stack.pop_back();
    RestoreStateFromRecord(buffer, redo_record);
}

void TextEditor::RestoreStateFromRecord(EditorBuffer& buffer, const UndoRecord& record) {
    // Clear existing content
    Line* p = buffer.document_head;
    while (p != nullptr) { Line* q = p; p = p->next; delete q; }
    buffer.document_head = nullptr;

    // Rebuild linked list
    Line* current = nullptr;
    for(const auto& line_str : record.lines) {
        Line* new_line = new Line{line_str};
        if (buffer.document_head == nullptr) { buffer.document_head = current = new_line; } 
        else { current->next = new_line; new_line->prev = current; current = new_line; }
    }
    if (buffer.document_head == nullptr) { // Handle case where file becomes empty
        buffer.document_head = new Line();
    }
    buffer.total_lines = record.lines.size();

    // Restore cursor position and pointers
    buffer.current_line_num = record.cursor_line_num;
    buffer.cursor_col = record.cursor_col;

    // Find the correct Line* for current_line
    buffer.current_line = buffer.document_head;
    for (int i = 1; i < buffer.current_line_num; ++i) {
        if (buffer.current_line && buffer.current_line->next) {
            buffer.current_line = buffer.current_line->next;
        }
    }
    
    // Restore the viewport
    buffer.first_visible_line = buffer.document_head;
    for (int i = 1; i < record.first_visible_line_num; ++i) {
        if (buffer.first_visible_line && buffer.first_visible_line->next) {
            buffer.first_visible_line = buffer.first_visible_line->next;
        }
    }
    
    buffer.cursor_screen_y = m_text_area_start_y + (record.cursor_line_num - record.first_visible_line_num);

    update_cursor_and_scroll();
}

void TextEditor::GoToNextWord() {
    if (m_current_buffer_idx == -1) return;
    EditorBuffer& buffer = currentBuffer();
    const std::string& line_text = buffer.current_line->text;
    int pos = buffer.cursor_col - 1;

    if (pos >= (int)line_text.length()) {
        if (buffer.current_line->next) {
            buffer.cursor_screen_y++; buffer.current_line = buffer.current_line->next; buffer.current_line_num++; buffer.cursor_col = 1;
        }
        return;
    }
    while (pos < (int)line_text.length() && !isspace(line_text[pos])) { pos++; }
    while (pos < (int)line_text.length() && isspace(line_text[pos])) { pos++; }
    buffer.cursor_col = pos + 1;
}

void TextEditor::GoToPreviousWord() {
    if (m_current_buffer_idx == -1) return;
    EditorBuffer& buffer = currentBuffer();
    int pos = buffer.cursor_col - 2;
    if (pos < 0) {
        if (buffer.current_line->prev) {
            buffer.cursor_screen_y--; buffer.current_line = buffer.current_line->prev; buffer.current_line_num--;
            buffer.cursor_col = buffer.current_line->text.length() + 1;
        }
        return;
    }
    const std::string& line_text = buffer.current_line->text;
    while (pos >= 0 && isspace(line_text[pos])) { pos--; }
    while (pos >= 0 && !isspace(line_text[pos])) { pos--; }
    buffer.cursor_col = pos + 2;
}

void TextEditor::GoToNextParagraph() {
    if (m_current_buffer_idx == -1 || !currentBuffer().current_line->next) return;
    EditorBuffer& buffer = currentBuffer();
    Line* p = buffer.current_line;
    bool found_text_after_cursor = false;
    while(p->next) {
        if (!p->text.empty()) { found_text_after_cursor = true; }
        if (found_text_after_cursor && p->text.empty()) { break; }
        p = p->next; buffer.cursor_screen_y++; buffer.current_line_num++;
    }
    while(p->next && p->text.empty()) {
        p = p->next; buffer.cursor_screen_y++; buffer.current_line_num++;
    }
    buffer.current_line = p;
    buffer.cursor_col = 1;
}

void TextEditor::GoToPreviousParagraph() {
    if (m_current_buffer_idx == -1 || !currentBuffer().current_line->prev) return;
    EditorBuffer& buffer = currentBuffer();
    Line* p = buffer.current_line;
    bool found_text_before_cursor = false;
    while(p->prev) {
        if (!p->text.empty()) { found_text_before_cursor = true; }
        if (p->prev && found_text_before_cursor && p->prev->text.empty()) {
             p = p->prev; buffer.cursor_screen_y--; buffer.current_line_num--;
             break;
        }
         p = p->prev; buffer.cursor_screen_y--; buffer.current_line_num--;
    }
    buffer.current_line = p;
    buffer.cursor_col = 1;
}

void TextEditor::process_key(wint_t ch) {
    // Handle global hotkeys first
    switch(ch) {
        case 14: DoNew(); return; // Ctrl+N
        case 15: selectfile(); return; // Ctrl+O
        case 19: // Ctrl+S
            if (m_current_buffer_idx != -1) {
                 if (currentBuffer().is_new_file) { SaveFileBrowser(); } else { write_file(currentBuffer()); }
            }
            return;
    }
    
    if (ch == KEY_F(10)) { ActivateMenuBar(1); return; }
    if (ch == 27) { if (m_current_buffer_idx != -1 && currentBuffer().selecting) { ClearSelection(); } else { nodelay(stdscr, FALSE); timeout(10); wint_t next_ch = m_renderer->getChar(); timeout(-1); nodelay(stdscr, TRUE); if (next_ch != ERR) { handle_alt_key(next_ch); } } return; }
    if (ch >= 128 && ch < 256) { char base_char = tolower(ch & 0x7F); if (base_char == 'f' || base_char == 'e' || base_char == 's' || base_char == 'v' || base_char == 'b' || base_char == 'w' || base_char == 'o' || base_char == 'h' || base_char == 'x') { handle_alt_key(base_char); return; } }
    
    if (m_current_buffer_idx == -1) return;
    
    if (ch == 3) { HandleCopy(); return; } if (ch == 24) { HandleCut(); return; } if (ch == 22) { HandlePaste(); return; }

    // Any key beyond this point is a modification, so create an undo point
    if ( (ch > 31 && ch < KEY_MIN) || ch == KEY_ENTER || ch == 10 || ch == 13 || ch == KEY_BACKSPACE || ch == 127 || ch == 8 || ch == KEY_DC ) {
        CreateUndoPoint(currentBuffer());
    }

    EditorBuffer& buffer = currentBuffer();
    bool should_delete_selection = buffer.selecting;
    switch(ch) {
        case KEY_SR: case KEY_SF: case KEY_SLEFT: case KEY_SRIGHT: case KEY_SHOME: case KEY_SEND:
        case KEY_SPREVIOUS: case KEY_SNEXT:
        case KEY_SHIFT_CTRL_LEFT: case KEY_SHIFT_CTRL_RIGHT: case KEY_SHIFT_CTRL_UP: case KEY_SHIFT_CTRL_DOWN:
             should_delete_selection = false; break;
        case KEY_UP: case KEY_DOWN: case KEY_LEFT: case KEY_RIGHT: case KEY_HOME: case KEY_END:
        case KEY_PPAGE: case KEY_NPAGE:
        case KEY_CTRL_LEFT: case KEY_CTRL_RIGHT: case KEY_CTRL_UP: case KEY_CTRL_DOWN:
            ClearSelection(); should_delete_selection = false; break;
    }
    if(should_delete_selection) DeleteSelection();
    
    switch(ch) {
        case KEY_F(2): if (buffer.is_new_file) { SaveFileBrowser(); } else { write_file(buffer); } break;
        case KEY_F(3): selectfile(); break;
        case KEY_F(1): ActivateMenuBar(8); break;
        case KEY_F(6): NextWindow(); break;
        case KEY_F(18): PreviousWindow(); break; // Shift+F6 is KEY_F(6+12)
        case KEY_CTRL_W: CloseWindow(); break;
        case KEY_UP: if (buffer.current_line->prev) { buffer.cursor_screen_y--; buffer.current_line = buffer.current_line->prev; buffer.current_line_num--; } break;
        case KEY_DOWN: if (buffer.current_line->next) { buffer.cursor_screen_y++; buffer.current_line = buffer.current_line->next; buffer.current_line_num++; } break;
        case KEY_LEFT: if (buffer.cursor_col > 1) { buffer.cursor_col--; } else if (buffer.current_line->prev) { buffer.cursor_screen_y--; buffer.current_line = buffer.current_line->prev; buffer.current_line_num--; buffer.cursor_col = buffer.current_line->text.length() + 1; } break;
        case KEY_RIGHT: if (buffer.cursor_col <= (int)buffer.current_line->text.length()) { buffer.cursor_col++; } else if (buffer.current_line->next) { buffer.cursor_screen_y++; buffer.current_line = buffer.current_line->next; buffer.current_line_num++; buffer.cursor_col = 1; } break;
        case KEY_HOME: buffer.cursor_col = 1; break;
        case KEY_END: buffer.cursor_col = buffer.current_line->text.length() + 1; break;
        case KEY_PPAGE: { int h = m_text_area_end_y - m_text_area_start_y + 1; for(int i=0;i<h && buffer.current_line->prev; ++i) {buffer.cursor_screen_y--; buffer.current_line=buffer.current_line->prev; buffer.current_line_num--;} } break;
        case KEY_NPAGE: { int h = m_text_area_end_y - m_text_area_start_y + 1; for(int i=0;i<h && buffer.current_line->next; ++i) {buffer.cursor_screen_y++; buffer.current_line=buffer.current_line->next; buffer.current_line_num++;} } break;
        case KEY_CTRL_LEFT: GoToPreviousWord(); break;
        case KEY_CTRL_RIGHT: GoToNextWord(); break;
        case KEY_CTRL_UP: GoToPreviousParagraph(); break;
        case KEY_CTRL_DOWN: GoToNextParagraph(); break;
        
        case KEY_SR: if (!buffer.selecting) { buffer.selecting = true; buffer.selection_anchor_line = buffer.current_line; buffer.selection_anchor_col = buffer.cursor_col; buffer.selection_anchor_linenum = buffer.current_line_num; } if (buffer.current_line->prev) { buffer.cursor_screen_y--; buffer.current_line = buffer.current_line->prev; buffer.current_line_num--; } UpdateSelection(); break;
        case KEY_SF: if (!buffer.selecting) { buffer.selecting = true; buffer.selection_anchor_line = buffer.current_line; buffer.selection_anchor_col = buffer.cursor_col; buffer.selection_anchor_linenum = buffer.current_line_num; } if (buffer.current_line->next) { buffer.cursor_screen_y++; buffer.current_line = buffer.current_line->next; buffer.current_line_num++; } UpdateSelection(); break;
        case KEY_SLEFT: if (!buffer.selecting) { buffer.selecting = true; buffer.selection_anchor_line = buffer.current_line; buffer.selection_anchor_col = buffer.cursor_col; buffer.selection_anchor_linenum = buffer.current_line_num; } if (buffer.cursor_col > 1) { buffer.cursor_col--; } else if (buffer.current_line->prev) { buffer.cursor_screen_y--; buffer.current_line = buffer.current_line->prev; buffer.current_line_num--; buffer.cursor_col = buffer.current_line->text.length() + 1; } UpdateSelection(); break;
        case KEY_SRIGHT: if (!buffer.selecting) { buffer.selecting = true; buffer.selection_anchor_line = buffer.current_line; buffer.selection_anchor_col = buffer.cursor_col; buffer.selection_anchor_linenum = buffer.current_line_num; } if (buffer.cursor_col <= (int)buffer.current_line->text.length()) { buffer.cursor_col++; } else if (buffer.current_line->next) { buffer.cursor_screen_y++; buffer.current_line = buffer.current_line->next; buffer.current_line_num++; buffer.cursor_col = 1; } UpdateSelection(); break;
        case KEY_SHOME: if (!buffer.selecting) { buffer.selecting = true; buffer.selection_anchor_line = buffer.current_line; buffer.selection_anchor_col = buffer.cursor_col; buffer.selection_anchor_linenum = buffer.current_line_num; } buffer.cursor_col = 1; UpdateSelection(); break;
        case KEY_SEND: if (!buffer.selecting) { buffer.selecting = true; buffer.selection_anchor_line = buffer.current_line; buffer.selection_anchor_col = buffer.cursor_col; buffer.selection_anchor_linenum = buffer.current_line_num; } buffer.cursor_col = buffer.current_line->text.length() + 1; UpdateSelection(); break;
        case KEY_SPREVIOUS: if (!buffer.selecting) { buffer.selecting = true; buffer.selection_anchor_line = buffer.current_line; buffer.selection_anchor_col = buffer.cursor_col; buffer.selection_anchor_linenum = buffer.current_line_num; } { int h = m_text_area_end_y - m_text_area_start_y + 1; for(int i=0;i<h && buffer.current_line->prev; ++i) {buffer.cursor_screen_y--; buffer.current_line=buffer.current_line->prev; buffer.current_line_num--;} } UpdateSelection(); break;
        case KEY_SNEXT: if (!buffer.selecting) { buffer.selecting = true; buffer.selection_anchor_line = buffer.current_line; buffer.selection_anchor_col = buffer.cursor_col; buffer.selection_anchor_linenum = buffer.current_line_num; } { int h = m_text_area_end_y - m_text_area_start_y + 1; for(int i=0;i<h && buffer.current_line->next; ++i) {buffer.cursor_screen_y++; buffer.current_line=buffer.current_line->next; buffer.current_line_num++;} } UpdateSelection(); break;
        case KEY_SHIFT_CTRL_LEFT: if (!buffer.selecting) { buffer.selecting = true; buffer.selection_anchor_line = buffer.current_line; buffer.selection_anchor_col = buffer.cursor_col; buffer.selection_anchor_linenum = buffer.current_line_num; } GoToPreviousWord(); UpdateSelection(); break;
        case KEY_SHIFT_CTRL_RIGHT: if (!buffer.selecting) { buffer.selecting = true; buffer.selection_anchor_line = buffer.current_line; buffer.selection_anchor_col = buffer.cursor_col; buffer.selection_anchor_linenum = buffer.current_line_num; } GoToNextWord(); UpdateSelection(); break;
        case KEY_SHIFT_CTRL_UP: if (!buffer.selecting) { buffer.selecting = true; buffer.selection_anchor_line = buffer.current_line; buffer.selection_anchor_col = buffer.cursor_col; buffer.selection_anchor_linenum = buffer.current_line_num; } GoToPreviousParagraph(); UpdateSelection(); break;
        case KEY_SHIFT_CTRL_DOWN: if (!buffer.selecting) { buffer.selecting = true; buffer.selection_anchor_line = buffer.current_line; buffer.selection_anchor_col = buffer.cursor_col; buffer.selection_anchor_linenum = buffer.current_line_num; } GoToNextParagraph(); UpdateSelection(); break;
        
        case KEY_ENTER: case 10: case 13: {
            std::string remainder = (buffer.cursor_col <= (int)buffer.current_line->text.length()) ? buffer.current_line->text.substr(buffer.cursor_col - 1) : "";
            if (buffer.cursor_col <= (int)buffer.current_line->text.length()) { buffer.current_line->text.erase(buffer.cursor_col - 1); }
            insert_line_after(buffer, buffer.current_line, remainder);
            buffer.cursor_screen_y++; buffer.current_line = buffer.current_line->next; buffer.current_line_num++; buffer.cursor_col = 1; buffer.horizontal_scroll_offset = 1;
            break;
        }
        case KEY_BACKSPACE: case 127: case 8:
            if (buffer.cursor_col > 1) { buffer.current_line->text.erase(buffer.cursor_col - 2, 1); buffer.cursor_col--; buffer.changed = true; } 
            else if (buffer.current_line->prev) {
                Line* to_delete = buffer.current_line; buffer.cursor_col = buffer.current_line->prev->text.length() + 1;
                buffer.current_line->prev->text += buffer.current_line->text; buffer.cursor_screen_y--; buffer.current_line = buffer.current_line->prev; buffer.current_line_num--;
                buffer.current_line->next = to_delete->next; if (to_delete->next) to_delete->next->prev = buffer.current_line;
                delete to_delete; buffer.changed = true;
            }
            break;
        case KEY_DC:
             if (buffer.cursor_col <= (int)buffer.current_line->text.length()) { buffer.current_line->text.erase(buffer.cursor_col - 1, 1); buffer.changed = true; } 
             else if (buffer.current_line->next) {
                Line* to_delete = buffer.current_line->next; buffer.current_line->text += to_delete->text;
                buffer.current_line->next = to_delete->next; if(to_delete->next) to_delete->next->prev = buffer.current_line;
                delete to_delete; buffer.changed = true;
            }
            break;
        case KEY_IC: buffer.insert_mode = !buffer.insert_mode; break;

        default:
            if (ch > 31 && ch < KEY_MIN) {
                std::string utf8_char = wchar_to_utf8(ch);
                if (buffer.insert_mode) { buffer.current_line->text.insert(buffer.cursor_col - 1, utf8_char); } 
                else {
                    if (buffer.cursor_col <= (int)buffer.current_line->text.length()) { buffer.current_line->text.replace(buffer.cursor_col - 1, 1, utf8_char); }
                    else { buffer.current_line->text += utf8_char; }
                }
                buffer.cursor_col++; buffer.changed = true;
            }
            break;
    }
}


void TextEditor::DoNew() {
    m_buffers.emplace_back();
    m_current_buffer_idx = m_buffers.size() - 1;
    read_file(currentBuffer()); 
}

void TextEditor::selectfile() {
    OpenFileBrowser();
}

void TextEditor::OpenFileBrowser() {
    char CWD_BUFFER[1024];
    if (!getcwd(CWD_BUFFER, sizeof(CWD_BUFFER))) { msgwin("Error: Cannot get current directory."); return; }
    std::string current_path(CWD_BUFFER);

    int h = m_renderer->getHeight() - 10; int w = m_renderer->getWidth() - 20;
    if (h < 10) h = 10; if (w < 40) w = 40;
    int starty = (m_renderer->getHeight() - h) / 2; int startx = (m_renderer->getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);
    
    m_renderer->hideCursor(); nodelay(stdscr, FALSE);

    std::vector<FileEntry> entries;
    int selection = 0; int top_of_list = 0;

    bool browser_active = true;
    while (browser_active) {
        entries.clear();
        DIR *dir = opendir(current_path.c_str());
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) != nullptr) {
                std::string name = ent->d_name;
                if (name == "." || name == "..") continue;
                std::string full_path = current_path + "/" + name;
                struct stat st;
                bool is_dir = false;
                if (stat(full_path.c_str(), &st) == 0) { is_dir = S_ISDIR(st.st_mode); }
                entries.push_back({name, is_dir});
            }
            closedir(dir);
        }
        entries.insert(entries.begin(), {"..", true}); entries.insert(entries.begin(), {".", true});
        std::sort(entries.begin() + 2, entries.end(), [](const FileEntry& a, const FileEntry& b){
            if (a.is_directory != b.is_directory) return a.is_directory;
            return a.name < b.name;
        });

        bool needs_redraw = true;
        while(true) {
            if (needs_redraw) {
                m_renderer->drawShadow(startx, starty, w, h);
                std::string title = " Open File - " + current_path;
                if (title.length() > (size_t)w - 4) { title = "..." + title.substr(title.length() - (w - 7)); }
                m_renderer->drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::SINGLE, title, Renderer::CP_DIALOG_TITLE, A_BOLD);

                int list_height = h - 2;
                for (int i = 0; i < list_height; ++i) {
                     m_renderer->drawText(startx + 1, starty + 1 + i, std::string(w - 2, ' '), Renderer::CP_DIALOG);
                    int entry_idx = top_of_list + i;
                    if (entry_idx < (int)entries.size()) {
                        FileEntry& entry = entries[entry_idx];
                        std::string display_name = entry.name;
                        if (entry.is_directory) display_name += "/";
                        if (display_name.length() > (size_t)w - 4) { display_name = display_name.substr(0, w - 7) + "..."; }
                        int color = (entry_idx == selection) ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG;
                        m_renderer->drawText(startx + 2, starty + 1 + i, display_name, color);
                    }
                }
                m_renderer->refresh();
                needs_redraw = false;
            }

            wint_t ch = m_renderer->getChar();
            switch (ch) {
                case KEY_UP: if (selection > 0) selection--; if (selection < top_of_list) top_of_list = selection; needs_redraw = true; break;
                case KEY_DOWN: if (selection < (int)entries.size() - 1) selection++; if (selection >= top_of_list + h - 2) top_of_list++; needs_redraw = true; break;
                case KEY_PPAGE: selection -= (h - 2); if (selection < 0) selection = 0; top_of_list = selection; needs_redraw = true; break;
                case KEY_NPAGE: selection += (h - 2); if (selection >= (int)entries.size()) selection = entries.size() - 1; top_of_list = selection - (h - 3); if(top_of_list < 0) top_of_list = 0; needs_redraw = true; break;
                case 27: browser_active = false; goto end_browser;
                case KEY_ENTER: case 10: case 13:
                    if (selection < (int)entries.size()) {
                        FileEntry& selected_entry = entries[selection];
                        if (selected_entry.is_directory) {
                            if (chdir((current_path + "/" + selected_entry.name).c_str()) == 0) {
                                char new_cwd[1024]; getcwd(new_cwd, sizeof(new_cwd)); current_path = new_cwd;
                                selection = 0; top_of_list = 0;
                                goto next_dir;
                            }
                        } else {
                            std::string new_filename = current_path + "/" + selected_entry.name;
                            for(size_t i = 0; i < m_buffers.size(); ++i) {
                                if (m_buffers[i].filename == new_filename) {
                                    SwitchToBuffer(i);
                                    browser_active = false; goto end_browser;
                                }
                            }
                            DoNew();
                            currentBuffer().filename = new_filename;
                            read_file(currentBuffer());
                            browser_active = false; goto end_browser;
                        }
                    }
                    break;
            }
        }
        next_dir:;
    }
    
    end_browser:
    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE); delwin(behind);
    nodelay(stdscr, TRUE); m_renderer->showCursor(); handleResize();
}


void TextEditor::SaveFileBrowser() {
    if (m_current_buffer_idx == -1) return;
    EditorBuffer& buffer = currentBuffer();

    char CWD_BUFFER[1024];
    if (!getcwd(CWD_BUFFER, sizeof(CWD_BUFFER))) { msgwin("Error: Cannot get current directory."); return; }
    std::string current_path(CWD_BUFFER);
    std::string filename_buffer = buffer.is_new_file ? "" : buffer.filename;

    int h = m_renderer->getHeight() - 10; int w = m_renderer->getWidth() - 20;
    if (h < 12) h = 12; if (w < 50) w = 50;
    int starty = (m_renderer->getHeight() - h) / 2; int startx = (m_renderer->getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);
    nodelay(stdscr, FALSE);

    std::vector<FileEntry> entries;
    int selection = 0; int top_of_list = 0;

    bool browser_active = true;
    while (browser_active) {
        entries.clear();
        DIR *dir = opendir(current_path.c_str());
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) != nullptr) {
                std::string name = ent->d_name;
                if (name == "." || name == "..") continue;
                 std::string full_path = current_path + "/" + name;
                struct stat st;
                bool is_dir = false;
                if (stat(full_path.c_str(), &st) == 0) is_dir = S_ISDIR(st.st_mode);
                entries.push_back({name, is_dir});
            }
            closedir(dir);
        }
        entries.insert(entries.begin(), {"..", true}); entries.insert(entries.begin(), {".", true});
        std::sort(entries.begin() + 2, entries.end(), [](const FileEntry& a, const FileEntry& b){
            if (a.is_directory != b.is_directory) return a.is_directory;
            return a.name < b.name;
        });

        bool needs_redraw = true;
        while(true) {
            if (needs_redraw) {
                m_renderer->hideCursor();
                m_renderer->drawShadow(startx, starty, w, h);
                std::string title = " Save File As - " + current_path;
                if (title.length() > (size_t)w - 4) title = "..." + title.substr(title.length() - (w - 7));
                m_renderer->drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::SINGLE, title, Renderer::CP_DIALOG_TITLE, A_BOLD);

                int list_height = h - 4;
                for (int i = 0; i < list_height; ++i) {
                    m_renderer->drawText(startx + 1, starty + 1 + i, std::string(w - 2, ' '), Renderer::CP_DIALOG);
                    int entry_idx = top_of_list + i;
                    if (entry_idx < (int)entries.size()) {
                        FileEntry& entry = entries[entry_idx];
                        std::string display_name = entry.name;
                        if (entry.is_directory) display_name += "/";
                        if (display_name.length() > (size_t)w - 4) display_name = display_name.substr(0, w - 7) + "...";
                        int color = (entry_idx == selection) ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG;
                        m_renderer->drawText(startx + 2, starty + 1 + i, display_name, color);
                    }
                }

                wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
                mvaddch(starty + h - 3, startx, ACS_LTEE);
                mvhline(starty + h - 3, startx + 1, ACS_HLINE, w-2);
                mvaddch(starty + h - 3, startx + w - 1, ACS_RTEE);
                wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

                std::string input_prompt = " Save As: ";
                m_renderer->drawText(startx + 1, starty + h - 2, std::string(w - 2, ' '), Renderer::CP_DIALOG);
                m_renderer->drawText(startx + 2, starty + h - 2, input_prompt + filename_buffer, Renderer::CP_DIALOG);

                m_renderer->showCursor();
                move(starty + h - 2, startx + 2 + input_prompt.length() + filename_buffer.length());
                m_renderer->refresh();
                needs_redraw = false;
            }

            wint_t ch = m_renderer->getChar();
            switch (ch) {
                case KEY_UP: if (selection > 0) selection--; if (selection < top_of_list) top_of_list = selection; needs_redraw = true; break;
                case KEY_DOWN: if (selection < (int)entries.size() - 1) selection++; if (selection >= top_of_list + h - 4) top_of_list++; needs_redraw = true; break;
                case KEY_PPAGE: selection -= (h - 4); if (selection < 0) selection = 0; top_of_list = selection; needs_redraw = true; break;
                case KEY_NPAGE: selection += (h - 4); if (selection >= (int)entries.size()) selection = entries.size() - 1; top_of_list = selection - (h - 5); if(top_of_list < 0) top_of_list = 0; needs_redraw = true; break;
                case 27: browser_active = false; goto end_save_browser;
                case KEY_BACKSPACE: case 127: case 8: if (!filename_buffer.empty()) filename_buffer.pop_back(); needs_redraw = true; break;
                case KEY_ENTER: case 10: case 13:
                    if (!filename_buffer.empty()) {
                        std::string new_filename = current_path + "/" + filename_buffer;
                        buffer.filename = new_filename;
                        write_file(buffer);
                        setSyntaxType(buffer);
                        browser_active = false;
                        goto end_save_browser;
                    } else if (selection < (int)entries.size()) {
                        FileEntry& selected_entry = entries[selection];
                        if (selected_entry.is_directory) {
                            if (chdir((current_path + "/" + selected_entry.name).c_str()) == 0) {
                                char new_cwd[1024]; getcwd(new_cwd, sizeof(new_cwd)); current_path = new_cwd;
                                selection = 0; top_of_list = 0;
                                goto next_save_dir;
                            }
                        } else { filename_buffer = selected_entry.name; needs_redraw = true; }
                    }
                    break;
                default: if (ch > 31 && ch < KEY_MIN) { filename_buffer += wchar_to_utf8(ch); needs_redraw = true; } break;
            }
        }
        next_save_dir:;
    }

    end_save_browser:
    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE); delwin(behind);
    nodelay(stdscr, TRUE); m_renderer->showCursor(); handleResize();
}


void TextEditor::ActivateMenuBar(int initial_menu_id) {
    m_renderer->hideCursor();

    int max_visible_menu_id = 0;
    int w = m_renderer->getWidth();
    for (size_t i = 0; i < m_menus.size(); ++i) {
        if (m_menu_positions[i] + m_menus[i].length() <= (size_t)w) {
            max_visible_menu_id = i + 1; 
        } else { break; }
    }

    if (max_visible_menu_id == 0) { m_renderer->showCursor(); return; }

    int current_id = initial_menu_id;
    if (current_id > max_visible_menu_id) { current_id = 1; }

    MenuAction action;
    std::map<int, std::pair<const std::vector<std::string>*, int>> menus_by_id = {
        {1, {&m_submenu_file, m_menu_positions[0] - 1}}, {2, {&m_submenu_edit, m_menu_positions[1] - 1}},
        {3, {&m_submenu_search, m_menu_positions[2] - 1}}, {4, {&m_submenu_view, m_menu_positions[3] - 1}},
        {5, {&m_submenu_build, m_menu_positions[4] - 1}}, {6, {&m_submenu_window, m_menu_positions[5] - 1}},
        {7, {&m_submenu_options, m_menu_positions[6] - 1}}, {8, {&m_submenu_help, m_menu_positions[7] - 1}}
    };
    
    do {
        drawEditorState(current_id);
        m_renderer->refresh();
        auto menu_info = menus_by_id.at(current_id);
        action = CallSubMenu(*menu_info.first, menu_info.second, 1, current_id);

        if (action == RESIZE_OCCURRED) { handleResize(); break; }
        if (action == NAVIGATE_RIGHT) { current_id++; if (current_id > max_visible_menu_id) current_id = 1; } 
        else if (action == NAVIGATE_LEFT) { current_id--; if (current_id < 1) current_id = max_visible_menu_id; }
    } while (action == NAVIGATE_LEFT || action == NAVIGATE_RIGHT);
    
    m_renderer->showCursor();
}

MenuAction TextEditor::CallSubMenu(const std::vector<std::string>& menuItems, int x, int y, int menu_id) {
    std::vector<std::string> finalMenuItems = menuItems;
    if (menu_id == 6) { // 6 is the new ID for the Window menu
        finalMenuItems.push_back(" ----------------- ");
        for(size_t i = 0; i < m_buffers.size() && i < 10; ++i) {
            std::string hotkey = (i < 9) ? std::to_string(i + 1) : "0";
            std::string item = " &" + hotkey + " " + m_buffers[i].filename;
            finalMenuItems.push_back(item);
        }
    }

    int w = 0;
    for(const auto& item : finalMenuItems) {
        if (item.length() > (size_t)w) w = item.length();
    }
    w += 4;
    int h = finalMenuItems.size() + 2;
    if (y + h > m_renderer->getHeight() || x + w > m_renderer->getWidth()) { return CLOSE_MENU; }

    WINDOW* behind = newwin(h + 1, w + 1, y, x);
    copywin(stdscr, behind, y, x, 0, 0, h, w, FALSE);
    
    m_renderer->drawShadow(x,y,w,h);
    nodelay(stdscr, FALSE);
    int selection = 1;
    wint_t ch;
    while(true) {
        m_renderer->drawBox(x, y, w, h, Renderer::CP_MENU_ITEM, Renderer::SINGLE);
        for (size_t i = 0; i < finalMenuItems.size(); ++i) {
            if(finalMenuItems[i].find("---") != std::string::npos) {
                wattron(stdscr, COLOR_PAIR(Renderer::CP_MENU_ITEM));
                mvaddch(y + 1 + i, x, ACS_LTEE); mvhline(y + 1 + i, x + 1, ACS_HLINE, w-2); mvaddch(y + 1 + i, x + w -1, ACS_RTEE);
                wattroff(stdscr, COLOR_PAIR(Renderer::CP_MENU_ITEM));
                continue;
            }
            m_renderer->drawText(x + 1, y + 1 + i, std::string(w - 2, ' '), Renderer::CP_MENU_ITEM);
            bool is_selected = (i + 1) == selection;
            int text_color = is_selected ? Renderer::CP_MENU_SELECTED : Renderer::CP_MENU_ITEM;
            m_renderer->drawStyledText(x + 2, y + 1 + i, finalMenuItems[i], text_color);
        }
        m_renderer->refresh();
        ch = m_renderer->getChar();
        switch (ch) {
            case KEY_UP:   if (selection > 1) selection--; else selection = finalMenuItems.size(); if(finalMenuItems[selection-1].find("---") != std::string::npos) if(selection>1) selection--; else selection = finalMenuItems.size(); break;
            case KEY_DOWN: if (selection < (int)finalMenuItems.size()) selection++; else selection = 1; if(finalMenuItems[selection-1].find("---") != std::string::npos) if(selection<finalMenuItems.size()) selection++; else selection=1; break;
            case KEY_LEFT:  copywin(behind, stdscr, 0, 0, y, x, h, w, FALSE); delwin(behind); nodelay(stdscr, TRUE); return NAVIGATE_LEFT;
            case KEY_RIGHT: copywin(behind, stdscr, 0, 0, y, x, h, w, FALSE); delwin(behind); nodelay(stdscr, TRUE); return NAVIGATE_RIGHT;
            case KEY_RESIZE: copywin(behind, stdscr, 0, 0, y, x, h, w, FALSE); delwin(behind); nodelay(stdscr, TRUE); return RESIZE_OCCURRED;
            case 27: copywin(behind, stdscr, 0, 0, y, x, h, w, FALSE); delwin(behind); nodelay(stdscr, TRUE); return CLOSE_MENU;
            
            case KEY_ENTER: case 10: case 13:
            handle_selection:
                delwin(behind); drawEditorState(-1); nodelay(stdscr, TRUE);
                switch (menu_id) {
                    case 1: // File
                        if (selection == 1) DoNew();
                        else if (selection == 2) selectfile();
                        else if (selection == 4) { if (currentBuffer().is_new_file) SaveFileBrowser(); else write_file(currentBuffer()); }
                        else if (selection == 5) SaveFileBrowser();
                        else if (selection == 7) main_loop_running = false;
                        else noti();
                        break;
                    case 2: // Edit
                        if (selection == 1) HandleUndo();
                        else if (selection == 2) HandleRedo();
                        else if (selection == 4) HandleCut(); 
                        else if (selection == 5) HandleCopy();
                        else if (selection == 6) HandlePaste(); 
                        else if (selection == 7) DeleteSelection();
                        else noti();
                        break;
                    case 6: // Window
                        if (selection == 1) NextWindow();
                        else if (selection == 2) PreviousWindow();
                        else if (selection == 3) CloseWindow();
                        else if (selection > 4) { // File list
                            int buffer_idx = selection - 5;
                            if (buffer_idx < (int)m_buffers.size()) SwitchToBuffer(buffer_idx);
                        }
                        break;
                    case 7: // Options
                        noti(); break;
                    case 8: // Help
                        if (selection == 1) noti();
                        else if (selection == 2) about_box();
                        break;
                    default: noti(); break;
                }
                return ITEM_SELECTED;

            default: {
                if (ch > 31) {
                    wchar_t lower_ch = tolower(ch);
                    for (size_t i = 0; i < finalMenuItems.size(); ++i) {
                        size_t amp_pos = finalMenuItems[i].find('&');
                        if (amp_pos != std::string::npos && amp_pos + 1 < finalMenuItems[i].length()) {
                            wchar_t hotkey = tolower(finalMenuItems[i][amp_pos + 1]);
                            if (lower_ch == hotkey) {
                                if(finalMenuItems[i].find("---") != std::string::npos) continue;
                                selection = i + 1;
                                goto handle_selection;
                            }
                        }
                    }
                }
                break;
            }
        }
    }
}

void TextEditor::setSyntaxType(EditorBuffer& buffer) {
    buffer.syntax_type = EditorBuffer::NONE;
    std::string lower_filename = buffer.filename;
    std::transform(lower_filename.begin(), lower_filename.end(), lower_filename.begin(), ::tolower);

    if (ends_with(lower_filename, ".c") || ends_with(lower_filename, ".h")) { buffer.syntax_type = EditorBuffer::C_CPP; } 
    else if (ends_with(lower_filename, ".cpp") || ends_with(lower_filename, ".hpp") || ends_with(lower_filename, ".cxx")) { buffer.syntax_type = EditorBuffer::C_CPP; } 
    else if (lower_filename == "makefile" || lower_filename == "gnumakefile") { buffer.syntax_type = EditorBuffer::MAKEFILE; } 
    else if (lower_filename == "cmakelists.txt") { buffer.syntax_type = EditorBuffer::CMAKE; }
    loadKeywords(buffer);
}

void TextEditor::loadKeywords(EditorBuffer& buffer) {
    buffer.keywords.clear();
    if (buffer.syntax_type == EditorBuffer::C_CPP) {
        const std::vector<std::string> cpp_keywords = {
            "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern", "float", "for", "goto", "if", "int", "long", "register", "return", "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "class", "public", "private", "protected", "new", "delete", "this", "friend", "virtual", "inline", "try", "catch", "throw", "namespace", "using", "template", "typename", "true", "false", "bool", "asm", "explicit", "operator", "nullptr"
        };
        for (const auto& kw : cpp_keywords) buffer.keywords[kw] = Renderer::CP_SYNTAX_KEYWORD;
    } else if (buffer.syntax_type == EditorBuffer::CMAKE) {
        const std::vector<std::string> cmake_keywords = { "add_compile_definitions", "add_compile_options", "add_custom_command", "add_custom_target", "add_dependencies", "add_executable", "add_library", "add_link_options", "add_subdirectory", "add_test", "aux_source_directory", "break", "build_command", "cmake_minimum_required", "cmake_policy", "configure_file", "create_test_sourcelist", "define_property", "else", "elseif", "enable_language", "enable_testing", "endforeach", "endfunction", "endif", "endmacro", "endwhile", "execute_process", "export", "file", "find_file", "find_library", "find_package", "find_path", "find_program", "fltk_wrap_ui", "foreach", "function", "get_cmake_property", "get_directory_property", "get_filename_component", "get_property", "get_source_file_property", "get_target_property", "get_test_property", "if", "include", "include_directories", "include_external_msproject", "include_regular_expression", "install", "link_directories", "link_libraries", "list", "load_cache", "load_command", "macro", "mark_as_advanced", "math", "message", "option", "project", "qt_wrap_cpp", "qt_wrap_ui", "remove_definitions", "return", "separate_arguments", "set", "set_directory_properties", "set_property", "set_source_files_properties", "set_target_properties", "set_tests_properties", "site_name", "source_group", "string", "target_compile_definitions", "target_compile_features", "target_compile_options", "target_include_directories", "target_link_libraries", "target_link_options", "try_compile", "try_run", "unset", "variable_watch", "while" };
        for (const auto& kw : cmake_keywords) {
            std::string lower_kw = kw;
            std::transform(lower_kw.begin(), lower_kw.end(), lower_kw.begin(), ::tolower);
            buffer.keywords[lower_kw] = Renderer::CP_SYNTAX_KEYWORD;
        }
    }
}

std::vector<SyntaxToken> TextEditor::parseLine(const std::string& line) {
    if (m_current_buffer_idx == -1) return {};
    EditorBuffer& buffer = currentBuffer();
    std::vector<SyntaxToken> tokens;
    if (line.empty()) return tokens;

    if (buffer.syntax_type == EditorBuffer::C_CPP && !line.empty() && line[0] == '#') { tokens.push_back({line, Renderer::CP_SYNTAX_PREPROCESSOR}); return tokens; }
    if ((buffer.syntax_type == EditorBuffer::MAKEFILE || buffer.syntax_type == EditorBuffer::CMAKE) && !line.empty() && line[0] == '#') { tokens.push_back({line, Renderer::CP_SYNTAX_COMMENT}); return tokens; }

    if (buffer.syntax_type == EditorBuffer::MAKEFILE) {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos && line.find_first_of(" \t") > colon_pos && (colon_pos + 1 >= line.length() || line[colon_pos+1] != '=')) {
            tokens.push_back({line.substr(0, colon_pos), Renderer::CP_SYNTAX_KEYWORD, A_BOLD});
            if (colon_pos < line.length() -1) {
                auto remaining_tokens = parseLine(line.substr(colon_pos));
                tokens.insert(tokens.end(), remaining_tokens.begin(), remaining_tokens.end());
            } else { tokens.push_back({":", Renderer::CP_DEFAULT_TEXT}); }
            return tokens;
        }
    }

    for (size_t i = 0; i < line.length(); ) {
        if (buffer.syntax_type == EditorBuffer::C_CPP && line.substr(i, 2) == "//") { tokens.push_back({line.substr(i), Renderer::CP_SYNTAX_COMMENT}); break; }
        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i]; size_t end = i + 1;
            while (end < line.length() && (line[end] != quote || line[end-1] == '\\')) { end++; }
            if (end < line.length()) end++;
            tokens.push_back({line.substr(i, end - i), Renderer::CP_SYNTAX_STRING});
            i = end; continue;
        }
        if (isdigit(line[i])) {
            size_t start = i;
            while (i < line.length() && isdigit(line[i])) i++;
            tokens.push_back({line.substr(start, i - start), Renderer::CP_SYNTAX_NUMBER});
            continue;
        }
        if (isalpha(line[i]) || line[i] == '_') {
            size_t start = i;
            while (i < line.length() && (isalnum(line[i]) || line[i] == '_')) i++;
            std::string word = line.substr(start, i - start);
            std::string lookup_word = word;
            if (buffer.syntax_type == EditorBuffer::CMAKE) { std::transform(lookup_word.begin(), lookup_word.end(), lookup_word.begin(), ::tolower); }
            if (buffer.keywords.count(lookup_word)) { tokens.push_back({word, buffer.keywords.at(lookup_word), A_BOLD}); } 
            else { tokens.push_back({word, Renderer::CP_DEFAULT_TEXT}); }
            continue;
        }
        size_t start = i; i++;
        tokens.push_back({line.substr(start, 1), Renderer::CP_DEFAULT_TEXT});
    }
    return tokens;
}

void TextEditor::drawHighlightedLine(const std::vector<SyntaxToken>& tokens, int screen_y) {
    if (m_current_buffer_idx == -1) return;
    EditorBuffer& buffer = currentBuffer();
    int current_col = 1;
    int screen_x = m_text_area_start_x;
    
    for (const auto& token : tokens) {
        for (char c : token.text) {
            if (current_col >= buffer.horizontal_scroll_offset) {
                if (screen_x <= m_text_area_end_x) {
                    m_renderer->drawText(screen_x, screen_y, std::string(1, c), token.colorId, token.flags);
                    screen_x++;
                } else { return; }
            }
            current_col++;
        }
    }
}

void TextEditor::NextWindow() {
    if (m_buffers.size() > 1) {
        m_current_buffer_idx = (m_current_buffer_idx + 1) % m_buffers.size();
    }
}

void TextEditor::PreviousWindow() {
    if (m_buffers.size() > 1) {
        m_current_buffer_idx = (m_current_buffer_idx - 1 + m_buffers.size()) % m_buffers.size();
    }
}

void TextEditor::CloseWindow() {
    if (m_current_buffer_idx == -1) return;
    
    if (currentBuffer().changed) {
        int result = msgwin_yesno("Save changes to " + currentBuffer().filename + "?");
        if (result == 1) { // Yes
            if (currentBuffer().is_new_file) SaveFileBrowser();
            else write_file(currentBuffer());
        } else if (result == -1) { // Cancel
            return;
        }
    }

    m_buffers.erase(m_buffers.begin() + m_current_buffer_idx);

    if (m_buffers.empty()) {
        DoNew();
    } else {
        if (m_current_buffer_idx >= (int)m_buffers.size()) {
            m_current_buffer_idx = m_buffers.size() - 1;
        }
    }
}

void TextEditor::SwitchToBuffer(int index) {
    if (index >= 0 && index < (int)m_buffers.size()) {
        m_current_buffer_idx = index;
    }
}


// --- Main Execution ---
int main(int argc, char* argv[]) {
    TextEditor editor;
    editor.run(argc, argv);
    return 0;
}