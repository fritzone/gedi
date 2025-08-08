#include "TextEditor.h"

#include "utils.h"

#include <ncurses.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>

#include <fstream>
#include <thread>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <cstdio>
#include <regex>

// --- Key Code Defines ---
#define KEY_CTRL_F 6
#define KEY_CTRL_R 18
#define KEY_CTRL_LEFT 545
#define KEY_CTRL_RIGHT 560
#define KEY_CTRL_UP 566
#define KEY_CTRL_DOWN 525
#define KEY_SHIFT_CTRL_LEFT 546
#define KEY_SHIFT_CTRL_RIGHT 561
#define KEY_SHIFT_CTRL_UP 567
#define KEY_SHIFT_CTRL_DOWN 526
#define KEY_CTRL_W 23

struct FileEntry {
    std::string name;
    bool is_directory;
    off_t size;
    time_t mod_time;
    mode_t permissions;
    std::string owner;
    std::string group;
};


void TextEditor::run(int argc, char* argv[]) {
    m_renderer = std::make_unique<Renderer>();
    loadConfig();

    if (m_themes_data.contains(m_color_scheme_name)) {
        m_renderer->loadColors(m_themes_data[m_color_scheme_name]);
    } else {
        msgwin("Theme not found, using first available.");
        if (!m_themes_data.empty()) {
            m_renderer->loadColors(m_themes_data.begin().value());
        }
    }

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

    m_renderer->drawBox(box_x, box_y, box_w, box_h, Renderer::CP_DIALOG_TITLE, Renderer::BoxStyle::DOUBLE);

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

    buffer.in_multiline_comment = false;
    Line* p_find_comment = buffer.document_head;
    for (int i=1; i < buffer.current_line_num && p_find_comment != buffer.first_visible_line; ++i) {
        parseLine(buffer, p_find_comment->text);
        if (p_find_comment->next) p_find_comment = p_find_comment->next;
        else break;
    }


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
                auto tokens = parseLine(buffer, p->text);
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

    if (m_search_mode) {
        std::string search_prompt = "Search: " + m_search_term;
        m_renderer->drawText(1, h - 1, search_prompt, Renderer::CP_STATUS_BAR);
        return;
    }

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

    if (m_compile_output_visible) {
        drawCompileOutputWindow();
    }
}

int TextEditor::msgwin_yesno(const std::string& question, const std::string& filename_in) {
    m_renderer->hideCursor();

    int h = 8, w = 50;
    int starty = (m_renderer->getHeight() - h) / 2;
    int startx = (m_renderer->getWidth() - w) / 2;

    std::string filename = filename_in;

    int max_text_width = w - 4;
    if (filename.length() > (size_t)max_text_width) {
        filename = "..." + filename.substr(filename.length() - (max_text_width - 3));
    }

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

    m_renderer->drawShadow(startx, starty, w, h);
    m_renderer->drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::SINGLE, " Question ", Renderer::CP_DIALOG_BUTTON, 0);

    wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
    for (int i = 1; i < h - 1; ++i) {
        mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
    }
    wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

    m_renderer->drawText(startx + 2, starty + 2, question, Renderer::CP_DIALOG);
    m_renderer->drawText(startx + 4, starty + 3, filename, Renderer::CP_DIALOG, A_BOLD);

    int selection = 0;
    nodelay(stdscr, FALSE);
    int final_result = -1;

    while(true) {
        std::string yes_text_unselected = " Yes ";
        std::string no_text_unselected  = " No  ";
        std::string yes_text_selected   = "> Yes <";
        std::string no_text_selected    = "> No  <";

        std::string yes_display = (selection == 0) ? yes_text_selected : yes_text_unselected;
        std::string no_display  = (selection == 1) ? no_text_selected : no_text_unselected;

        int total_width = yes_display.length() + no_display.length() + 5;
        int yes_x = startx + (w - total_width) / 2;
        int no_x = yes_x + yes_display.length() + 5;
        int btn_y = starty + 5;

        m_renderer->drawText(startx + 1, btn_y, std::string(w - 2, ' '), Renderer::CP_DIALOG);

        if (selection == 0) {
            m_renderer->drawText(yes_x, btn_y, yes_display, Renderer::CP_DIALOG_BUTTON);
        } else {
            m_renderer->drawText(yes_x, btn_y, yes_display, Renderer::CP_DIALOG);
            m_renderer->drawText(yes_x + 1, btn_y, "Y", Renderer::CP_DIALOG_TITLE);
        }

        if (selection == 1) {
            m_renderer->drawText(no_x, btn_y, no_display, Renderer::CP_DIALOG_BUTTON);
        } else {
            m_renderer->drawText(no_x, btn_y, no_display, Renderer::CP_DIALOG);
            m_renderer->drawText(no_x + 1, btn_y, "N", Renderer::CP_DIALOG_TITLE);
        }

        m_renderer->refresh();
        wint_t ch = m_renderer->getChar();

        switch (ch) {
        case KEY_LEFT:
        case KEY_RIGHT:
        case 9:
            selection = 1 - selection;
            break;
        case 'y':
        case 'Y':
            final_result = 1;
            goto end_dialog;
        case 'n':
        case 'N':
            final_result = 0;
            goto end_dialog;
        case KEY_ENTER:
        case 10:
        case 13:
            final_result = (selection == 0) ? 1 : 0;
            goto end_dialog;
        case 27:
            final_result = -1;
            goto end_dialog;
        }
    }

end_dialog:
    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE); delwin(behind);
    nodelay(stdscr, TRUE);
    m_renderer->showCursor();
    return final_result;
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
        if (m_output_screen_visible) {
            showOutputScreen();
            continue;
        }

        update_cursor_and_scroll();
        drawEditorState();
        if (m_current_buffer_idx != -1) {
            if (m_search_mode) {
                m_renderer->setCursor(1 + strlen("Search: ") + m_search_term.length(), m_renderer->getHeight() - 1);
            } else if (!m_compile_output_visible) {
                EditorBuffer& buffer = currentBuffer();
                m_renderer->setCursor(buffer.cursor_col - buffer.horizontal_scroll_offset + m_text_area_start_x, buffer.cursor_screen_y);
            }
        }
        m_renderer->refresh();

        wint_t ch = m_renderer->getChar();

        if (ch == KEY_RESIZE) { handleResize(); continue; }

        if (ch != ERR) {
            if (m_compile_output_visible) {
                switch (ch) {
                case KEY_UP: {
                    int new_pos = m_compile_output_cursor_pos;
                    while (new_pos > 0) {
                        new_pos--;
                        if (m_compile_output_lines[new_pos].type != CMSG_NONE) {
                            m_compile_output_cursor_pos = new_pos;
                            break; // Found the previous message
                        }
                    }
                    break;
                }
                case KEY_DOWN: {
                    int new_pos = m_compile_output_cursor_pos;
                    while (new_pos < (int)m_compile_output_lines.size() - 1) {
                        new_pos++;
                        if (m_compile_output_lines[new_pos].type != CMSG_NONE) {
                            m_compile_output_cursor_pos = new_pos;
                            break; // Found the next message
                        }
                    }
                    break;
                }
                case 27: // ESC
                    // Restore original view state
                    currentBuffer().current_line_num = m_pre_compile_view_state.line_num;
                    currentBuffer().cursor_col = m_pre_compile_view_state.col;
                    // Recalculate pointers
                    currentBuffer().current_line = currentBuffer().document_head;
                    for(int i = 1; i < currentBuffer().current_line_num; ++i) {
                        if (currentBuffer().current_line->next) currentBuffer().current_line = currentBuffer().current_line->next;
                    }
                    m_compile_output_visible = false;
                    m_renderer->showCursor();
                    handleResize();
                    break;
                case KEY_ENTER:
                case 10:
                case 13:
                    if (m_compile_output_cursor_pos < (int)m_compile_output_lines.size()) {
                        const auto& msg = m_compile_output_lines[m_compile_output_cursor_pos];
                        if (msg.line != -1) {
                            currentBuffer().current_line_num = msg.line;
                            currentBuffer().cursor_col = msg.col;
                            // Recalculate pointers
                            currentBuffer().current_line = currentBuffer().document_head;
                            for(int i = 1; i < msg.line; ++i) {
                                if (currentBuffer().current_line->next) currentBuffer().current_line = currentBuffer().current_line->next;
                            }
                            update_cursor_and_scroll();
                        }
                    }
                    m_compile_output_visible = false;
                    m_renderer->showCursor();
                    handleResize();
                    break;
                }
            } else if (ch == 27 || ch == KEY_F(10)) {
                process_key(ch);
            } else {
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

    if (buffer.cursor_col > (int)buffer.current_line->text.length() + 1) {
        buffer.cursor_col = buffer.current_line->text.length() + 1;
    }
    if (buffer.cursor_col < 1) {
        buffer.cursor_col = 1;
    }

    int first_visible_linenum = 1;
    Line* p = buffer.document_head;
    while (p && p != buffer.first_visible_line) {
        p = p->next;
        first_visible_linenum++;
    }

    int page_height = m_text_area_end_y - m_text_area_start_y + 1;
    if (page_height <= 0) return;

    if (buffer.current_line_num < first_visible_linenum) {
        buffer.first_visible_line = buffer.current_line;
        first_visible_linenum = buffer.current_line_num;
    }
    else if (buffer.current_line_num >= first_visible_linenum + page_height) {
        Line* new_first_visible = buffer.current_line;
        for (int i = 0; i < page_height - 1; ++i) {
            if (new_first_visible->prev) {
                new_first_visible = new_first_visible->prev;
            } else {
                break;
            }
        }
        buffer.first_visible_line = new_first_visible;
        first_visible_linenum = buffer.current_line_num - (page_height - 1);
    }

    buffer.cursor_screen_y = m_text_area_start_y + (buffer.current_line_num - first_visible_linenum);

    int text_area_width = m_text_area_end_x - m_text_area_start_x + 1;
    if (text_area_width <= 0) return;

    if (buffer.cursor_col < buffer.horizontal_scroll_offset) {
        buffer.horizontal_scroll_offset = buffer.cursor_col;
    }
    else if (buffer.cursor_col >= buffer.horizontal_scroll_offset + text_area_width) {
        buffer.horizontal_scroll_offset = buffer.cursor_col - text_area_width + 1;
    }
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
    if (buffer.undo_stack.size() > 100) {
        buffer.undo_stack.erase(buffer.undo_stack.begin());
    }
    buffer.redo_stack.clear();
}

void TextEditor::HandleUndo() {
    if (m_current_buffer_idx == -1 || currentBuffer().undo_stack.empty()) return;

    EditorBuffer& buffer = currentBuffer();

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

    UndoRecord undo_record = buffer.undo_stack.back();
    buffer.undo_stack.pop_back();
    RestoreStateFromRecord(buffer, undo_record);
}

void TextEditor::HandleRedo() {
    if (m_current_buffer_idx == -1 || currentBuffer().redo_stack.empty()) return;

    EditorBuffer& buffer = currentBuffer();

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

    UndoRecord redo_record = buffer.redo_stack.back();
    buffer.redo_stack.pop_back();
    RestoreStateFromRecord(buffer, redo_record);
}

void TextEditor::RestoreStateFromRecord(EditorBuffer& buffer, const UndoRecord& record) {
    Line* p = buffer.document_head;
    while (p != nullptr) { Line* q = p; p = p->next; delete q; }
    buffer.document_head = nullptr;

    Line* current = nullptr;
    for(const auto& line_str : record.lines) {
        Line* new_line = new Line{line_str};
        if (buffer.document_head == nullptr) { buffer.document_head = current = new_line; }
        else { current->next = new_line; new_line->prev = current; current = new_line; }
    }
    if (buffer.document_head == nullptr) {
        buffer.document_head = new Line();
    }
    buffer.total_lines = record.lines.size();

    buffer.current_line_num = record.cursor_line_num;
    buffer.cursor_col = record.cursor_col;

    buffer.current_line = buffer.document_head;
    for (int i = 1; i < buffer.current_line_num; ++i) {
        if (buffer.current_line && buffer.current_line->next) {
            buffer.current_line = buffer.current_line->next;
        }
    }

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
    if (m_search_mode) {
        switch(ch) {
        case 27: // ESC key
            DeactivateSearch();
            break;
        case KEY_ENTER: case 10: case 13:
            PerformSearch(true);
            break;
        case KEY_BACKSPACE: case 127: case 8:
            if (!m_search_term.empty()) {
                m_search_term.pop_back();
                if (m_search_term.length() <= 2) ClearSelection();
            }
            break;
        default:
            if (ch > 31 && ch < KEY_MIN) {
                m_search_term += wchar_to_utf8(ch);
                if (m_search_term.length() > 2) {
                    PerformSearch(false);
                }
            }
            break;
        }
        return;
    }

    // Handle global hotkeys first
    switch(ch) {
    case 14: DoNew(); return; // Ctrl+N
    case 15: selectfile(); return; // Ctrl+O
    case 19: // Ctrl+S
        if (m_current_buffer_idx != -1) {
            if (currentBuffer().is_new_file) { SaveFileBrowser(); } else { write_file(currentBuffer()); }
        }
        return;
    case KEY_CTRL_F: ActivateSearch(); return;
    case KEY_CTRL_R: ActivateReplace(); return;
    case KEY_F(9): compileAndRun(); return;
    case KEY_F(9+12): compileOnly(); return; // Shift+F9
    case KEY_F(5):
        m_output_screen_visible = !m_output_screen_visible;
        if (!m_output_screen_visible) {
            handleResize();
        }
        return;
    }

    if (ch == KEY_F(10)) { ActivateMenuBar(1); return; }
    if (ch == 27) { // ESC key
        if (m_current_buffer_idx != -1 && currentBuffer().selecting) {
            ClearSelection();
        } else {
            nodelay(stdscr, FALSE);
            timeout(50);
            wint_t next_ch = m_renderer->getChar();
            timeout(-1);
            nodelay(stdscr, TRUE);

            if (next_ch != ERR) {
                handle_alt_key(next_ch);
            }
        }
        return;
    }
    if (ch >= 128 && ch < 256) { char base_char = tolower(ch & 0x7F); if (base_char == 'f' || base_char == 'e' || base_char == 's' || base_char == 'v' || base_char == 'b' || base_char == 'w' || base_char == 'o' || base_char == 'h' || base_char == 'x') { handle_alt_key(base_char); return; } }

    if (m_current_buffer_idx == -1) return;

    if (ch == 3) { HandleCopy(); return; } if (ch == 24) { HandleCut(); return; } if (ch == 22) { HandlePaste(); return; }

    if ( (ch > 31 && ch < KEY_MIN) || ch == KEY_ENTER || ch == 10 || ch == 13 || ch == KEY_BACKSPACE || ch == 127 || ch == 8 || ch == 9 || ch == KEY_DC ) {
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
    case KEY_F(18): PreviousWindow(); break;
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
    case 9: { // Tab key
        const std::string& line_text = buffer.current_line->text;
        int cursor_idx = buffer.cursor_col - 1;

        size_t first_char_pos = line_text.find_first_not_of(" \t");

        if (first_char_pos != std::string::npos && cursor_idx < first_char_pos)
        {
            buffer.cursor_col = first_char_pos + 1;
        }
        else
        {
            std::string spaces_to_insert(m_indentation_width, ' ');

            if (cursor_idx > (int)line_text.length()) {
                cursor_idx = line_text.length();
            }

            buffer.current_line->text.insert(cursor_idx, spaces_to_insert);
            buffer.cursor_col += m_indentation_width;
            buffer.changed = true;
        }
        break;
    }

    case KEY_ENTER: case 10: case 13: {
        std::string remainder = (buffer.cursor_col <= (int)buffer.current_line->text.length()) ? buffer.current_line->text.substr(buffer.cursor_col - 1) : "";
        if (buffer.cursor_col <= (int)buffer.current_line->text.length()) {
            buffer.current_line->text.erase(buffer.cursor_col - 1);
        }

        std::string indent_str;

        if (m_smart_indentation) {
            const std::string& prev_line_text = buffer.current_line->text;

            size_t indent_end_pos = prev_line_text.find_first_not_of(" \t");
            if (indent_end_pos != std::string::npos) {
                indent_str = prev_line_text.substr(0, indent_end_pos);
            } else {
                indent_str = prev_line_text;
            }

            std::string effective_line = prev_line_text;
            size_t comment_pos = effective_line.find("//");
            if (comment_pos != std::string::npos) {
                effective_line = effective_line.substr(0, comment_pos);
            }

            size_t last_char_pos = effective_line.find_last_not_of(" \t");
            if (last_char_pos != std::string::npos && effective_line[last_char_pos] == '{') {
                indent_str += std::string(m_indentation_width, ' ');
            }
        }

        std::string new_line_text = indent_str + remainder;
        int new_cursor_col = indent_str.length() + 1;

        insert_line_after(buffer, buffer.current_line, new_line_text);

        buffer.cursor_screen_y++;
        buffer.current_line = buffer.current_line->next;
        buffer.current_line_num++;
        buffer.cursor_col = new_cursor_col;
        buffer.horizontal_scroll_offset = 1;
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
    static int new_file_counter = 0;
    std::string filename;

    // Loop to find a unique filename that doesn't exist in the current directory
    do {
        std::stringstream ss;
        ss << "noname" << std::setw(2) << std::setfill('0') << new_file_counter++ << ".cpp";
        filename = ss.str();
    } while (std::filesystem::exists(filename));

    // The loop found a unique name and the counter is now ready for the next file.

    m_buffers.emplace_back(); // Creates a buffer with default values
    m_current_buffer_idx = m_buffers.size() - 1;

    // Now, override the default filename and call read_file to correctly initialize it as empty
    currentBuffer().filename = filename;
    currentBuffer().is_new_file = true; // Make sure it's marked as new
    read_file(currentBuffer()); // This will handle creating the empty line and setting syntax
}

void TextEditor::selectfile() {
    OpenFileBrowser();
}

void TextEditor::OpenFileBrowser() {
    char CWD_BUFFER[1024];
    getcwd(CWD_BUFFER, sizeof(CWD_BUFFER));
    std::string current_path(CWD_BUFFER);

    int h = m_renderer->getHeight() - 8;
    int w = m_renderer->getWidth() - 12;
    if (h < 12) h = 12; if (w < 60) w = 60;
    int starty = (m_renderer->getHeight() - h) / 2;
    int startx = (m_renderer->getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

    m_renderer->hideCursor();
    nodelay(stdscr, FALSE);

    std::vector<FileEntry> entries;
    int selection = 0;
    int top_of_list = 0;
    std::string search_string; // For find-as-you-type

    bool browser_active = true;
    while (browser_active) {
        entries.clear();
        DIR *dir = opendir(current_path.c_str());
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) != nullptr) {
                std::string name = ent->d_name;
                std::string full_path = current_path + "/" + name;
                struct stat st;
                if (stat(full_path.c_str(), &st) == 0) {
                    struct passwd *pw = getpwuid(st.st_uid);
                    struct group  *gr = getgrgid(st.st_gid);
                    std::string owner = (pw != NULL) ? pw->pw_name : std::to_string(st.st_uid);
                    std::string group = (gr != NULL) ? gr->gr_name : std::to_string(st.st_gid);
                    entries.push_back({name, S_ISDIR(st.st_mode), st.st_size, st.st_mtime, st.st_mode, owner, group});
                }
            }
            closedir(dir);
        }
        std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b){
            if (a.name == ".") return true; if (b.name == ".") return false;
            if (a.name == "..") return true; if (b.name == "..") return false;
            if (a.is_directory != b.is_directory) return a.is_directory;
            return a.name < b.name;
        });

        bool needs_redraw = true;
        while(true) {
            if (needs_redraw) {
                // --- Draw Layout ---
                m_renderer->drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::SINGLE, " Open File ", Renderer::CP_DIALOG_TITLE, A_BOLD);
                wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
                for (int i = 1; i < h - 1; ++i) mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
                wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

                std::string path_str = " " + current_path;
                if (path_str.length() > (size_t)w - 2) path_str = "..." + path_str.substr(path_str.length() - (w - 5));
                m_renderer->drawText(startx + 1, starty + 1, std::string(w - 2, ' '), Renderer::CP_HIGHLIGHT);
                m_renderer->drawText(startx + 2, starty + 1, path_str, Renderer::CP_HIGHLIGHT);

                int list_w = (w * 2) / 3;
                int details_x = startx + list_w + 1;
                wattron(stdscr, COLOR_PAIR(Renderer::CP_DEFAULT_TEXT));
                mvvline(starty + 2, details_x - 1, ACS_VLINE, h-3);
                wattroff(stdscr, COLOR_PAIR(Renderer::CP_DEFAULT_TEXT));

                int list_height = h - 4; // Make space for search string
                wattron(stdscr, COLOR_PAIR(Renderer::CP_LIST_BOX));
                for(int i=0; i<list_height; ++i) mvwaddstr(stdscr, starty + 2 + i, startx + 1, std::string(list_w - 2, ' ').c_str());
                wattroff(stdscr, COLOR_PAIR(Renderer::CP_LIST_BOX));

                for (int i = 0; i < list_height; ++i) {
                    int entry_idx = top_of_list + i;
                    if (entry_idx < (int)entries.size()) {
                        FileEntry& entry = entries[entry_idx];
                        std::string display_name = entry.name;
                        if (entry.is_directory && entry.name != "." && entry.name != "..") display_name += "/";
                        if (display_name.length() > (size_t)list_w - 4) display_name = display_name.substr(0, list_w-7) + "...";

                        int color = (entry_idx == selection) ? Renderer::CP_MENU_SELECTED : Renderer::CP_LIST_BOX;
                        int style = (entry.is_directory && entry_idx != selection) ? A_BOLD : 0;
                        m_renderer->drawText(startx + 2, starty + 2 + i, display_name, color, style);
                    }
                }

                if (selection < (int)entries.size()) {
                    FileEntry& selected = entries[selection];
                    int field_y = starty + 3;
                    m_renderer->drawText(details_x + 2, field_y, "Type:", Renderer::CP_DIALOG);
                    m_renderer->drawText(details_x + 4, field_y + 1, selected.is_directory ? "Directory" : "File", Renderer::CP_DIALOG, A_BOLD);
                    field_y += 3;
                    m_renderer->drawText(details_x + 2, field_y, "Owner:", Renderer::CP_DIALOG);
                    m_renderer->drawText(details_x + 4, field_y + 1, selected.owner + ":" + selected.group, Renderer::CP_DIALOG, A_BOLD);
                    field_y += 3;
                    m_renderer->drawText(details_x + 2, field_y, "Perms:", Renderer::CP_DIALOG);
                    m_renderer->drawText(details_x + 4, field_y + 1, formatPermissions(selected.permissions), Renderer::CP_DIALOG, A_BOLD);
                    field_y += 3;
                    if (!selected.is_directory) {
                        m_renderer->drawText(details_x + 2, field_y, "Size:", Renderer::CP_DIALOG);
                        m_renderer->drawText(details_x + 4, field_y + 1, formatSize(selected.size), Renderer::CP_DIALOG, A_BOLD);
                        field_y += 3;
                    }
                    m_renderer->drawText(details_x + 2, field_y, "Modified:", Renderer::CP_DIALOG);
                    m_renderer->drawText(details_x + 4, field_y + 1, formatTime(selected.mod_time), Renderer::CP_DIALOG, A_BOLD);
                }

                // Draw the search string
                std::string search_prompt = "Find: " + search_string;
                m_renderer->drawText(startx + 1, starty + h - 2, std::string(w - 2, ' '), Renderer::CP_LIST_BOX);
                m_renderer->drawText(startx + 2, starty + h - 2, search_prompt, Renderer::CP_LIST_BOX);


                m_renderer->refresh();
                needs_redraw = false;
            }

            wint_t ch = m_renderer->getChar();

            switch (ch) {
            case KEY_UP: if (selection > 0) { selection--; needs_redraw = true; } if (selection < top_of_list) top_of_list = selection; search_string.clear(); break;
            case KEY_DOWN: if (selection < (int)entries.size() - 1) { selection++; needs_redraw = true; } if (selection >= top_of_list + h - 4) top_of_list++; search_string.clear(); break;
            case 27: browser_active = false; goto end_browser;
            case KEY_ENTER: case 10: case 13:
                if (selection < (int)entries.size()) {
                    FileEntry& selected_entry = entries[selection];
                    if (selected_entry.is_directory) {
                        if (chdir((current_path + "/" + selected_entry.name).c_str()) == 0) {
                            getcwd(CWD_BUFFER, sizeof(CWD_BUFFER)); current_path = CWD_BUFFER;
                            selection = 0; top_of_list = 0; search_string.clear();
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
            case KEY_BACKSPACE: case 127: case 8:
                if (!search_string.empty()) {
                    search_string.pop_back();
                    needs_redraw = true;
                }
                break;
            default:
                if (ch > 31 && ch < KEY_MIN) {
                    search_string += tolower(ch);

                    for (size_t i = 0; i < entries.size(); ++i) {
                        std::string lower_name = entries[i].name;
                        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                        if (lower_name.rfind(search_string, 0) == 0) {
                            selection = i;
                            if (selection < top_of_list || selection >= top_of_list + h - 4) {
                                top_of_list = selection;
                            }
                            break;
                        }
                    }
                    needs_redraw = true;
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
    getcwd(CWD_BUFFER, sizeof(CWD_BUFFER));
    std::string current_path(CWD_BUFFER);

    size_t last_slash = buffer.filename.find_last_of('/');
    std::string filename_buffer = (last_slash != std::string::npos) ? buffer.filename.substr(last_slash + 1) : buffer.filename;

    int h = m_renderer->getHeight() - 8;
    int w = m_renderer->getWidth() - 12;
    if (h < 14) h = 14; if (w < 60) w = 60;
    int starty = (m_renderer->getHeight() - h) / 2;
    int startx = (m_renderer->getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);
    nodelay(stdscr, FALSE);

    std::vector<FileEntry> entries;
    int selection = 0;
    int top_of_list = 0;

    bool browser_active = true;
    while (browser_active) {
        entries.clear();
        DIR *dir = opendir(current_path.c_str());
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) != nullptr) {
                std::string name = ent->d_name;
                std::string full_path = current_path + "/" + name;
                struct stat st;
                if (stat(full_path.c_str(), &st) == 0) {
                    struct passwd *pw = getpwuid(st.st_uid);
                    struct group  *gr = getgrgid(st.st_gid);
                    std::string owner = (pw != NULL) ? pw->pw_name : std::to_string(st.st_uid);
                    std::string group = (gr != NULL) ? gr->gr_name : std::to_string(st.st_gid);
                    entries.push_back({name, S_ISDIR(st.st_mode), st.st_size, st.st_mtime, st.st_mode, owner, group});
                }
            }
            closedir(dir);
        }
        std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b){
            if (a.name == ".") return true; if (b.name == ".") return false;
            if (a.name == "..") return true; if (b.name == "..") return false;
            if (a.is_directory != b.is_directory) return a.is_directory;
            return a.name < b.name;
        });

        bool needs_redraw = true;
        while(true) {
            if (needs_redraw) {
                // --- Draw Layout ---
                m_renderer->drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::SINGLE, " Save File As ", Renderer::CP_DIALOG_TITLE, A_BOLD);
                wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
                for (int i = 1; i < h - 1; ++i) mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
                wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

                std::string path_str = " " + current_path;
                if (path_str.length() > (size_t)w - 2) path_str = "..." + path_str.substr(path_str.length() - (w - 5));
                m_renderer->drawText(startx + 1, starty + 1, std::string(w - 2, ' '), Renderer::CP_HIGHLIGHT);
                m_renderer->drawText(startx + 2, starty + 1, path_str, Renderer::CP_HIGHLIGHT);

                int list_w = (w * 2) / 3;
                int details_x = startx + list_w + 1;
                wattron(stdscr, COLOR_PAIR(Renderer::CP_DEFAULT_TEXT));
                mvvline(starty + 2, details_x - 1, ACS_VLINE, h - 5);
                mvhline(starty + h - 3, startx + 1, ACS_HLINE, w-2);
                wattroff(stdscr, COLOR_PAIR(Renderer::CP_DEFAULT_TEXT));

                int list_height = h - 5;
                wattron(stdscr, COLOR_PAIR(Renderer::CP_LIST_BOX));
                for(int i=0; i<list_height; ++i) mvwaddstr(stdscr, starty + 2 + i, startx + 1, std::string(list_w - 2, ' ').c_str());
                wattroff(stdscr, COLOR_PAIR(Renderer::CP_LIST_BOX));

                for (int i = 0; i < list_height; ++i) {
                    int entry_idx = top_of_list + i;
                    if (entry_idx < (int)entries.size()) {
                        FileEntry& entry = entries[entry_idx];
                        std::string display_name = entry.name;
                        if (entry.is_directory && entry.name != "." && entry.name != "..") display_name += "/";
                        if (display_name.length() > (size_t)list_w - 4) display_name = display_name.substr(0, list_w-7) + "...";

                        int color = (entry_idx == selection) ? Renderer::CP_MENU_SELECTED : Renderer::CP_LIST_BOX;
                        int style = (entry.is_directory && entry_idx != selection) ? A_BOLD : 0;
                        m_renderer->drawText(startx + 2, starty + 2 + i, display_name, color, style);
                    }
                }

                if (selection < (int)entries.size()) {
                    FileEntry& selected = entries[selection];
                    int field_y = starty + 3;
                    m_renderer->drawText(details_x + 2, field_y, "Type:", Renderer::CP_DIALOG);
                    m_renderer->drawText(details_x + 4, field_y + 1, selected.is_directory ? "Directory" : "File", Renderer::CP_DIALOG, A_BOLD);
                    field_y += 3;
                    m_renderer->drawText(details_x + 2, field_y, "Perms:", Renderer::CP_DIALOG);
                    m_renderer->drawText(details_x + 4, field_y + 1, formatPermissions(selected.permissions), Renderer::CP_DIALOG, A_BOLD);
                    field_y += 3;
                    if (!selected.is_directory) {
                        m_renderer->drawText(details_x + 2, field_y, "Size:", Renderer::CP_DIALOG);
                        m_renderer->drawText(details_x + 4, field_y + 1, formatSize(selected.size), Renderer::CP_DIALOG, A_BOLD);
                    }
                }

                std::string input_prompt = "Save Name: ";
                m_renderer->drawText(startx + 1, starty + h - 2, std::string(w - 2, ' '), Renderer::CP_LIST_BOX);
                m_renderer->drawText(startx + 2, starty + h - 2, input_prompt + filename_buffer, Renderer::CP_LIST_BOX);
                m_renderer->showCursor();
                move(starty + h - 2, startx + 2 + input_prompt.length() + filename_buffer.length());

                m_renderer->refresh();
                needs_redraw = false;
            }

            wint_t ch = m_renderer->getChar();
            bool search_list = true;

            switch (ch) {
            case KEY_UP:
                if (selection > 0) {
                    selection--;
                    if (!entries[selection].is_directory) filename_buffer = entries[selection].name;
                    else filename_buffer.clear();
                }
                if (selection < top_of_list) top_of_list = selection;
                needs_redraw = true;
                search_list = false;
                break;
            case KEY_DOWN:
                if (selection < (int)entries.size() - 1) {
                    selection++;
                    if (!entries[selection].is_directory) filename_buffer = entries[selection].name;
                    else filename_buffer.clear();
                }
                if (selection >= top_of_list + h - 5) top_of_list++;
                needs_redraw = true;
                search_list = false;
                break;
            case 27: browser_active = false; goto end_save_browser;
            case KEY_BACKSPACE: case 127: case 8: if (!filename_buffer.empty()) { filename_buffer.pop_back(); needs_redraw = true; } break;
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
                            getcwd(CWD_BUFFER, sizeof(CWD_BUFFER)); current_path = CWD_BUFFER;
                            selection = 0; top_of_list = 0; filename_buffer.clear();
                            goto next_save_dir;
                        }
                    } else { filename_buffer = selected_entry.name; needs_redraw = true; }
                }
                search_list = false;
                break;
            default: if (ch > 31 && ch < KEY_MIN) { filename_buffer += wchar_to_utf8(ch); needs_redraw = true; } else { search_list = false; } break;
            }

            if (search_list && !filename_buffer.empty()) {
                std::string lower_search = filename_buffer;
                std::transform(lower_search.begin(), lower_search.end(), lower_search.begin(), ::tolower);
                for (size_t i = 0; i < entries.size(); ++i) {
                    std::string lower_name = entries[i].name;
                    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                    if (lower_name.rfind(lower_search, 0) == 0) {
                        selection = i;
                        if (selection < top_of_list || selection >= top_of_list + h - 5) {
                            top_of_list = selection;
                        }
                        break;
                    }
                }
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
    if (menu_id == 6) { // Window menu
        finalMenuItems.push_back(" ----------------- ");
        for(size_t i = 0; i < m_buffers.size() && i < 10; ++i) {
            std::string hotkey_num = (i < 9) ? std::to_string(i + 1) : "0";
            std::string text_part = " &" + hotkey_num + " " + m_buffers[i].filename;
            std::string hotkey_part = "Alt+" + hotkey_num;

            // Define a total width for the menu item string to align hotkeys
            const int total_width = 28;

            // Truncate the text part if the whole string would be too long
            if (text_part.length() + hotkey_part.length() + 1 > total_width) {
                int available_len = total_width - hotkey_part.length() - 1 - 3; // -1 for space, -3 for "..."
                if (available_len < 5) available_len = 5;
                text_part = text_part.substr(0, available_len) + "...";
            }

            // Calculate padding to right-align the hotkey
            int padding = total_width - text_part.length() - hotkey_part.length();
            if (padding < 1) padding = 1;

            std::string item = text_part + std::string(padding, ' ') + hotkey_part;
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
            case 3: // Search
                if (selection == 1) ActivateSearch();
                else if (selection == 4) ActivateReplace();
                break;
            case 4: // View
                if (selection == 1) { // Output Screen
                    m_output_screen_visible = !m_output_screen_visible;
                    if (!m_output_screen_visible) handleResize();
                }
                break;
            case 5: // Build
                if (selection == 1) compileAndRun();
                else if (selection == 2) compileOnly();
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
                EditorSettingsDialog(); break;
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
    else if (ends_with(lower_filename, ".s") || ends_with(lower_filename, ".asm")) { buffer.syntax_type = EditorBuffer::ASSEMBLY; }
    else if (ends_with(lower_filename, ".ld")) { buffer.syntax_type = EditorBuffer::LD_SCRIPT; }
    else if (ends_with(lower_filename, ".glsl") || ends_with(lower_filename, ".vert") || ends_with(lower_filename, ".frag")) { buffer.syntax_type = EditorBuffer::GLSL; }
    loadKeywords(buffer);
}

void TextEditor::loadKeywords(EditorBuffer& buffer) {
    buffer.keywords.clear();
    if (buffer.syntax_type == EditorBuffer::C_CPP || buffer.syntax_type == EditorBuffer::GLSL) {
        const std::vector<std::string> keywords = { "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern", "float", "for", "goto", "if", "int", "long", "register", "return", "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "class", "public", "private", "protected", "new", "delete", "this", "friend", "virtual", "inline", "try", "catch", "throw", "namespace", "using", "template", "typename", "true", "false", "bool", "asm", "explicit", "operator", "nullptr" };
        for (const auto& kw : keywords) buffer.keywords[kw] = Renderer::CP_SYNTAX_KEYWORD;
    }
    if (buffer.syntax_type == EditorBuffer::GLSL) {
        const std::vector<std::string> glsl_keywords = { "in", "out", "inout", "uniform", "layout", "centroid", "smooth", "flat", "noperspective", "attribute", "varying", "buffer", "shared", "coherent", "volatile", "restrict", "readonly", "writeonly", "resource", "atomic_uint", "group", "local_size_x", "local_size_y", "local_size_z", "std140", "std430", "packed", "binding", "location", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "bvec2", "bvec3", "bvec4", "uvec2", "uvec3", "uvec4", "dvec2", "dvec3", "dvec4", "mat2", "mat3", "mat4", "dmat2", "dmat3", "dmat4", "sampler1D", "sampler2D", "sampler3D", "samplerCube", "sampler2DRect", "sampler1DShadow", "sampler2DShadow", "samplerCubeShadow", "sampler2DRectShadow", "sampler1DArray", "sampler2DArray", "sampler1DArrayShadow", "sampler2DArrayShadow", "isampler1D", "isampler2D", "isampler3D", "isamplerCube", "isampler2DRect", "isampler1DArray", "isampler2DArray", "usampler1D", "usampler2D", "usampler3D", "usamplerCube", "usampler2DRect", "usampler1DArray", "usampler2DArray", "samplerBuffer", "isamplerBuffer", "usamplerBuffer", "sampler2DMS", "isampler2DMS", "usampler2DMS", "sampler2DMSArray", "isampler2DMSArray", "usampler2DMSArray", "image1D", "iimage1D", "uimage1D", "image2D", "iimage2D", "uimage2D", "image3D", "iimage3D", "uimage3D", "image2DRect", "iimage2DRect", "uimage2DRect", "imageCube", "iimageCube", "uimageCube", "imageBuffer", "iimageBuffer", "uimageBuffer", "image1DArray", "iimage1DArray", "uimage1DArray", "image2DArray", "iimage2DArray", "uimage2DArray", "image2DMS", "iimage2DMS", "uimage2DMS", "image2DMSArray", "iimage2DMSArray", "uimage2DMSArray", "discard", "precision", "highp", "mediump", "lowp" };
        for (const auto& kw : glsl_keywords) buffer.keywords[kw] = Renderer::CP_SYNTAX_KEYWORD;
    }
    else if (buffer.syntax_type == EditorBuffer::CMAKE) {
        const std::vector<std::string> cmake_keywords = { "add_compile_definitions", "add_compile_options", "add_custom_command", "add_custom_target", "add_dependencies", "add_executable", "add_library", "add_link_options", "add_subdirectory", "add_test", "aux_source_directory", "break", "build_command", "cmake_minimum_required", "cmake_policy", "configure_file", "create_test_sourcelist", "define_property", "else", "elseif", "enable_language", "enable_testing", "endforeach", "endfunction", "endif", "endmacro", "endwhile", "execute_process", "export", "file", "find_file", "find_library", "find_package", "find_path", "find_program", "fltk_wrap_ui", "foreach", "function", "get_cmake_property", "get_directory_property", "get_filename_component", "get_property", "get_source_file_property", "get_target_property", "get_test_property", "if", "include", "include_directories", "include_external_msproject", "include_regular_expression", "install", "link_directories", "link_libraries", "list", "load_cache", "load_command", "macro", "mark_as_advanced", "math", "message", "option", "project", "qt_wrap_cpp", "qt_wrap_ui", "remove_definitions", "return", "separate_arguments", "set", "set_directory_properties", "set_property", "set_source_files_properties", "set_target_properties", "set_tests_properties", "site_name", "source_group", "string", "target_compile_definitions", "target_compile_features", "target_compile_options", "target_include_directories", "target_link_libraries", "target_link_options", "try_compile", "try_run", "unset", "variable_watch", "while" };
        for (const auto& kw : cmake_keywords) {
            std::string lower_kw = kw;
            std::transform(lower_kw.begin(), lower_kw.end(), lower_kw.begin(), ::tolower);
            buffer.keywords[lower_kw] = Renderer::CP_SYNTAX_KEYWORD;
        }
    } else if (buffer.syntax_type == EditorBuffer::ASSEMBLY) {
        const std::vector<std::string> instructions = {"mov", "lea", "add", "sub", "mul", "imul", "div", "idiv", "inc", "dec", "and", "or", "xor", "not", "shl", "shr", "sal", "sar", "rol", "ror", "jmp", "je", "jne", "jz", "jnz", "jg", "jge", "jl", "jle", "ja", "jae", "jb", "jbe", "jc", "jnc", "call", "ret", "push", "pop", "cmp", "test", "syscall"};
        const std::vector<std::string> registers = {"rax", "eax", "ax", "al", "ah", "rbx", "ebx", "bx", "bl", "bh", "rcx", "ecx", "cx", "cl", "ch", "rdx", "edx", "dx", "dl", "dh", "rsi", "esi", "si", "sil", "rdi", "edi", "di", "dil", "rbp", "ebp", "bp", "bpl", "rsp", "esp", "sp", "spl", "r8", "r8d", "r8w", "r8b", "r9", "r9d", "r9w", "r9b", "r10", "r10d", "r10w", "r10b", "r11", "r11d", "r11w", "r11b", "r12", "r12d", "r12w", "r12b", "r13", "r13d", "r13w", "r13b", "r14", "r14d", "r14w", "r14b", "r15", "r15d", "r15w", "r15b"};
        const std::vector<std::string> directives = {".align", ".ascii", ".asciz", ".byte", ".data", ".double", ".equ", ".extern", ".file", ".float", ".global", ".globl", ".int", ".long", ".quad", ".section", ".short", ".size", ".string", ".text", ".type", ".word", ".zero"};
        for (const auto& kw : instructions) buffer.keywords[kw] = Renderer::CP_SYNTAX_KEYWORD;
        for (const auto& kw : registers) buffer.keywords["%" + kw] = Renderer::CP_SYNTAX_REGISTER_VAR;
        for (const auto& kw : directives) buffer.keywords[kw] = Renderer::CP_SYNTAX_PREPROCESSOR;
    } else if (buffer.syntax_type == EditorBuffer::MAKEFILE) {
        const std::vector<std::string> directives = {"if", "ifeq", "ifneq", "else", "endif", "include", "define", "endef", "override", "export", "undefine"};
        const std::vector<std::string> variables = {"CC", "CXX", "CPP", "LD", "AS", "AR", "CFLAGS", "CXXFLAGS", "LDFLAGS", "ASFLAGS", "ARFLAGS", "RM", "SHELL"};
        for (const auto& kw : directives) buffer.keywords[kw] = Renderer::CP_SYNTAX_PREPROCESSOR;
        for (const auto& kw : variables) buffer.keywords[kw] = Renderer::CP_SYNTAX_REGISTER_VAR;
    } else if (buffer.syntax_type == EditorBuffer::LD_SCRIPT) {
        const std::vector<std::string> keywords = {"ENTRY", "MEMORY", "SECTIONS", "INCLUDE", "OUTPUT_FORMAT", "OUTPUT_ARCH", "ASSERT", "ORIGIN", "LENGTH", "FILL"};
        const std::vector<std::string> functions = {"ALIGN", "DEFINED", "LOADADDR", "SIZEOF", "ADDR", "MAX", "MIN"};
        for (const auto& kw : keywords) buffer.keywords[kw] = Renderer::CP_SYNTAX_PREPROCESSOR;
        for (const auto& kw : functions) buffer.keywords[kw] = Renderer::CP_SYNTAX_KEYWORD;
    }
}

std::vector<SyntaxToken> TextEditor::parseLine(EditorBuffer& buffer, const std::string& line) {
    std::vector<SyntaxToken> tokens;
    if (line.empty()) {
        return tokens;
    }

    size_t i = 0;

    if (buffer.in_multiline_comment) {
        size_t end_comment = line.find("*/");
        if (end_comment != std::string::npos) {
            tokens.push_back({line.substr(0, end_comment + 2), Renderer::CP_SYNTAX_COMMENT});
            buffer.in_multiline_comment = false;
            i = end_comment + 2;
        } else {
            tokens.push_back({line, Renderer::CP_SYNTAX_COMMENT});
            return tokens;
        }
    }

    while (i < line.length()) {
        if ((buffer.syntax_type == EditorBuffer::C_CPP || buffer.syntax_type == EditorBuffer::GLSL || buffer.syntax_type == EditorBuffer::LD_SCRIPT) && line.substr(i, 2) == "/*") {
            size_t end_comment = line.find("*/", i + 2);
            if (end_comment != std::string::npos) {
                tokens.push_back({line.substr(i, end_comment + 2 - i), Renderer::CP_SYNTAX_COMMENT});
                i = end_comment + 2;
            } else {
                tokens.push_back({line.substr(i), Renderer::CP_SYNTAX_COMMENT});
                buffer.in_multiline_comment = true;
                break;
            }
            continue;
        }

        if ((buffer.syntax_type == EditorBuffer::C_CPP || buffer.syntax_type == EditorBuffer::GLSL) && line.substr(i, 2) == "//") {
            tokens.push_back({line.substr(i), Renderer::CP_SYNTAX_COMMENT});
            break;
        }
        if ((buffer.syntax_type == EditorBuffer::MAKEFILE || buffer.syntax_type == EditorBuffer::CMAKE || buffer.syntax_type == EditorBuffer::ASSEMBLY) && line[i] == '#') {
            tokens.push_back({line.substr(i), Renderer::CP_SYNTAX_COMMENT});
            break;
        }
        if (buffer.syntax_type == EditorBuffer::ASSEMBLY && line[i] == ';') {
            tokens.push_back({line.substr(i), Renderer::CP_SYNTAX_COMMENT});
            break;
        }

        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i];
            size_t end = i + 1;
            while (end < line.length() && (line[end] != quote || line[end - 1] == '\\')) {
                end++;
            }
            if (end < line.length()) end++;
            tokens.push_back({line.substr(i, end - i), Renderer::CP_SYNTAX_STRING});
            i = end;
            continue;
        }

        if (buffer.syntax_type == EditorBuffer::MAKEFILE && (line.substr(i, 2) == "$(" || line.substr(i, 2) == "${")) {
            char end_char = (line[i+1] == '(') ? ')' : '}';
            size_t end = line.find(end_char, i + 2);
            if (end != std::string::npos) {
                tokens.push_back({line.substr(i, end + 1 - i), Renderer::CP_SYNTAX_STRING});
                i = end + 1;
            } else {
                tokens.push_back({line.substr(i, 2), Renderer::CP_DEFAULT_TEXT});
                i += 2;
            }
            continue;
        }

        if (isdigit(line[i])) {
            size_t start = i;
            while (i < line.length() && isdigit(line[i])) i++;
            tokens.push_back({line.substr(start, i - start), Renderer::CP_SYNTAX_NUMBER});
            continue;
        }

        if (buffer.syntax_type == EditorBuffer::ASSEMBLY && (line[i] == '%' || line[i] == '.')) {
            size_t start = i;
            i++;
            while (i < line.length() && isalnum(line[i])) i++;
            std::string word = line.substr(start, i - start);
            if (buffer.keywords.count(word)) {
                tokens.push_back({word, buffer.keywords.at(word)});
            } else {
                tokens.push_back({word, Renderer::CP_DEFAULT_TEXT});
            }
            continue;
        }

        if (isalpha(line[i]) || line[i] == '_') {
            size_t start = i;
            while (i < line.length() && (isalnum(line[i]) || line[i] == '_')) i++;
            std::string word = line.substr(start, i - start);
            std::string lookup_word = word;
            if (buffer.syntax_type == EditorBuffer::CMAKE) {
                std::transform(lookup_word.begin(), lookup_word.end(), lookup_word.begin(), ::tolower);
            }

            if (buffer.syntax_type == EditorBuffer::MAKEFILE && i < line.length() && line[i] == ':' && (i + 1 >= line.length() || line[i+1] != '=')) {
                tokens.push_back({word, Renderer::CP_SYNTAX_KEYWORD, A_BOLD});
            } else if (buffer.keywords.count(lookup_word)) {
                int color = buffer.keywords.at(lookup_word);
                int flags = m_renderer->getStyleFlags(static_cast<Renderer::ColorPairID>(color));
                tokens.push_back({word, color, flags});
            } else {
                tokens.push_back({word, Renderer::CP_DEFAULT_TEXT});
            }
            continue;
        }

        tokens.push_back({line.substr(i, 1), Renderer::CP_DEFAULT_TEXT});
        i++;
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
        int result = msgwin_yesno("Save changes to file?", currentBuffer().filename);
        if (result == 1) {
            if (currentBuffer().is_new_file) SaveFileBrowser();
            else write_file(currentBuffer());
        } else if (result == -1) {
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

void TextEditor::ActivateSearch() {
    if (m_current_buffer_idx == -1) return;

    ClearSelection();
    m_search_mode = true;
    m_search_term.clear();

    EditorBuffer& buffer = currentBuffer();
    m_search_origin.line_num = buffer.current_line_num;
    m_search_origin.col = buffer.cursor_col;

    int fv_linenum = 1;
    Line* p = buffer.document_head;
    while (p && p != buffer.first_visible_line) {
        p = p->next;
        fv_linenum++;
    }
    m_search_origin.first_visible_line_num = fv_linenum;
}

void TextEditor::DeactivateSearch() {
    if (!m_search_mode) return;

    EditorBuffer& buffer = currentBuffer();

    if (buffer.selecting) {
        buffer.current_line = buffer.selection_anchor_line;
        buffer.current_line_num = buffer.selection_anchor_linenum;
        buffer.cursor_col = buffer.selection_anchor_col;
    }

    m_search_mode = false;
    m_search_term.clear();
    ClearSelection();

    update_cursor_and_scroll();
}

void TextEditor::PerformSearch(bool next) {
    if (m_search_term.empty() || m_current_buffer_idx == -1) return;

    EditorBuffer& buffer = currentBuffer();

    Line* start_line = buffer.current_line;
    int start_line_num = buffer.current_line_num;
    int start_col = next ? buffer.cursor_col : 0;

    std::string lower_search_term = m_search_term;
    std::transform(lower_search_term.begin(), lower_search_term.end(), lower_search_term.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    Line* p = start_line;
    int current_line_num = start_line_num;
    int lines_searched = 0;

    while (lines_searched <= buffer.total_lines) {
        std::string lower_text = p->text;
        std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        size_t found_pos = lower_text.find(lower_search_term, start_col);

        if (found_pos != std::string::npos) {
            buffer.current_line = p;
            buffer.current_line_num = current_line_num;
            buffer.cursor_col = found_pos + 1;

            buffer.selecting = true;
            buffer.selection_anchor_line = p;
            buffer.selection_anchor_linenum = current_line_num;
            buffer.selection_anchor_col = found_pos + 1;
            buffer.cursor_col += m_search_term.length();
            UpdateSelection();

            update_cursor_and_scroll();
            return;
        }

        p = p->next;
        current_line_num++;
        if (p == nullptr) {
            p = buffer.document_head;
            current_line_num = 1;
        }
        start_col = 0;
        lines_searched++;

        if (p == start_line) break;
    }

    ClearSelection();
}

void TextEditor::createDefaultConfigFile() {
    json j;
    j["smart_indentation"] = true;
    j["indentation_width"] = 4;
    j["color_scheme"] = "colors.json";
    std::ofstream o("config.json");
    o << std::setw(4) << j << std::endl;
}

void TextEditor::loadConfig() {
    if (!std::filesystem::exists("config.json")) {
        createDefaultConfigFile();
    }
    try {
        std::ifstream f("config.json");
        json data = json::parse(f);
        if (data.contains("smart_indentation") && data["smart_indentation"].is_boolean()) {
            m_smart_indentation = data["smart_indentation"];
        }
        if (data.contains("indentation_width") && data["indentation_width"].is_number_integer()) {
            m_indentation_width = data["indentation_width"];
        }
        if (data.contains("color_scheme") && data["color_scheme"].is_string()) {
            m_color_scheme_name = data["color_scheme"];
        }
    } catch (const json::parse_error& e) {
        msgwin("Error parsing config.json. Using defaults.");
    }

    if (std::filesystem::exists("colors.json")) {
        try {
            std::ifstream f("colors.json");
            m_themes_data = json::parse(f);
        } catch (const json::parse_error& e) {
            msgwin("Error parsing colors.json!");
        }
    }
}

void TextEditor::saveConfig() {
    json j;
    j["smart_indentation"] = m_smart_indentation;
    j["indentation_width"] = m_indentation_width;
    j["color_scheme"] = m_color_scheme_name;
    std::ofstream o("config.json");
    o << std::setw(4) << j << std::endl;
}

void TextEditor::EditorSettingsDialog() {
    m_renderer->hideCursor();

    bool temp_smart_indent = m_smart_indentation;
    int temp_indent_width = m_indentation_width;
    std::string temp_color_scheme = m_color_scheme_name;

    std::vector<std::string> themes;
    for (auto const& [key, val] : m_themes_data.items()) {
        themes.push_back(key);
    }
    std::sort(themes.begin(), themes.end());

    int theme_idx = 0;
    for(size_t i = 0; i < themes.size(); ++i) {
        if (themes[i] == temp_color_scheme) {
            theme_idx = i;
            break;
        }
    }

    int h = 10 + themes.size();
    if (h > m_renderer->getHeight() - 4) h = m_renderer->getHeight() - 4;
    int w = 50;
    int starty = (m_renderer->getHeight() - h) / 2;
    int startx = (m_renderer->getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

    int focus = 0;
    nodelay(stdscr, FALSE);

    while(true) {
        m_renderer->drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::SINGLE, " Editor Settings ", Renderer::CP_DIALOG_TITLE, A_BOLD);
        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        for (int i = 1; i < h - 1; ++i) mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        m_renderer->drawText(startx + 3, starty + 2, "Smart Indentation", focus == 0 ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
        m_renderer->drawText(startx + w - 6, starty + 2, temp_smart_indent ? "[X]" : "[ ]", focus == 0 ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);

        m_renderer->drawText(startx + 3, starty + 3, "Indentation Width", focus == 1 ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
        m_renderer->drawText(startx + w - 8, starty + 3, "< " + std::to_string(temp_indent_width) + " >", focus == 1 ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);

        m_renderer->drawText(startx + 3, starty + 5, "Color Scheme:", focus == 2 ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);

        int list_height = h - 8;
        int list_start_y = starty + 6;
        int top_of_list = 0;
        if (theme_idx >= list_height) {
            top_of_list = theme_idx - list_height + 1;
        }
        for(int i = 0; i < list_height; ++i) {
            int current_theme_idx = top_of_list + i;
            if (current_theme_idx < (int)themes.size()) {
                m_renderer->drawText(startx + 5, list_start_y + i, themes[current_theme_idx], current_theme_idx == theme_idx ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
            }
        }

        m_renderer->drawText(startx + 10, starty + h - 2, " Save ", focus == 3 ? Renderer::CP_DIALOG_BUTTON : Renderer::CP_DIALOG);
        m_renderer->drawText(startx + w - 18, starty + h - 2, " Cancel ", focus == 4 ? Renderer::CP_DIALOG_BUTTON : Renderer::CP_DIALOG);
        m_renderer->refresh();

        wint_t ch = m_renderer->getChar();
        switch(ch) {
        case KEY_UP:
            if (focus == 2 && theme_idx > 0) theme_idx--;
            else if (focus > 0) focus--;
            break;
        case KEY_DOWN:
            if (focus == 2 && theme_idx < (int)themes.size() - 1) theme_idx++;
            else if (focus < 4) focus++;
            break;
        case 9: focus = (focus + 1) % 5; break;
        case KEY_LEFT: if (focus == 1 && temp_indent_width > 1) temp_indent_width--; break;
        case KEY_RIGHT: if (focus == 1 && temp_indent_width < 16) temp_indent_width++; break;
        case ' ': if (focus == 0) temp_smart_indent = !temp_smart_indent; break;
        case KEY_ENTER: case 10: case 13:
            if (focus == 0) temp_smart_indent = !temp_smart_indent;
            else if (focus == 3) goto save_and_exit;
            else if (focus == 4) goto cancel_and_exit;
            break;
        case 27: goto cancel_and_exit;
        }
    }

save_and_exit:
    m_smart_indentation = temp_smart_indent;
    m_indentation_width = temp_indent_width;
    if (!themes.empty()) m_color_scheme_name = themes[theme_idx];
    saveConfig();
    if (m_themes_data.contains(m_color_scheme_name)) {
        m_renderer->loadColors(m_themes_data[m_color_scheme_name]);
    }

cancel_and_exit:
    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE); delwin(behind);
    nodelay(stdscr, TRUE);
    m_renderer->showCursor();
}

void TextEditor::compileAndRun() {
    if (m_current_buffer_idx == -1) {
        msgwin("No file to compile.");
        return;
    }

    EditorBuffer& buffer = currentBuffer();
    if (buffer.changed) {
        if (buffer.is_new_file) {
            SaveFileBrowser();
            if (buffer.is_new_file) {
                msgwin("Save file before compiling.");
                return;
            }
        } else {
            write_file(buffer);
        }
    }

    def_prog_mode();
    endwin();

    std::string cguess_cmd = "python3 cguess.py \"" + buffer.filename + "\" 2>/dev/null";
    std::string compile_cmd;
    FILE* pipe = popen(cguess_cmd.c_str(), "r");
    if (!pipe) {
        reset_prog_mode();
        refresh();
        msgwin("Error: Could not run cguess.py script.");
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), pipe) != NULL) {
        if (strstr(line, "g++") != NULL || strstr(line, "clang++") != NULL) {
            compile_cmd = line;
            break;
        }
    }
    pclose(pipe);

    compile_cmd.erase(std::remove(compile_cmd.begin(), compile_cmd.end(), '\n'), compile_cmd.end());
    compile_cmd.erase(std::remove(compile_cmd.begin(), compile_cmd.end(), '\r'), compile_cmd.end());

    if (compile_cmd.empty()) {
        reset_prog_mode();
        refresh();
        msgwin("cguess.py failed to produce a command.");
        return;
    }

    m_output_content = "Compiling with command:\n" + compile_cmd + "\n\n";
    std::string temp_output_file = "tedit_compile_output.tmp";
    std::string full_compile_cmd = "(" + compile_cmd + ") > " + temp_output_file + " 2>&1";
    int compile_result = system(full_compile_cmd.c_str());

    std::ifstream output_stream(temp_output_file);
    std::string captured_output((std::istreambuf_iterator<char>(output_stream)), std::istreambuf_iterator<char>());
    output_stream.close();
    m_output_content += captured_output;

    if (compile_result == 0) {
        m_output_content += "\n--- Compilation Successful. Running program... ---\n\n";

        std::string exec_name;
        size_t o_pos = compile_cmd.find("-o ");
        if (o_pos != std::string::npos) {
            std::string temp = compile_cmd.substr(o_pos + 3);
            size_t space_pos = temp.find(' ');
            exec_name = temp.substr(0, space_pos);
        } else {
            size_t dot_pos = buffer.filename.find_last_of(".");
            exec_name = (dot_pos == std::string::npos) ? buffer.filename : buffer.filename.substr(0, dot_pos);
            size_t slash_pos = exec_name.find_last_of("/");
            if(slash_pos != std::string::npos) exec_name = exec_name.substr(slash_pos + 1);
        }

        std::string run_cmd = "./" + exec_name;
        std::string full_run_cmd = run_cmd + " >> " + temp_output_file + " 2>&1";
        system(full_run_cmd.c_str());

        std::ifstream run_output_stream(temp_output_file);
        std::string full_captured_output((std::istreambuf_iterator<char>(run_output_stream)), std::istreambuf_iterator<char>());
        run_output_stream.close();
        m_output_content = full_captured_output;
    } else {
        m_output_content += "\n--- Compilation Failed. ---\n";
    }

    m_output_content += "\n\n--- Press any key to return to the editor. ---";
    remove(temp_output_file.c_str());

    reset_prog_mode();
    refresh();

    m_output_screen_visible = true;
}

void TextEditor::showOutputScreen() {
    def_prog_mode();
    endwin();

    std::cout << "\033[2J\033[H" << m_output_content << std::flush;

    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

    reset_prog_mode();
    refresh();

    m_output_screen_visible = false;

    handleResize();
}

void TextEditor::compileOnly() {
    if (m_current_buffer_idx == -1) {
        msgwin("No file to compile.");
        return;
    }

    EditorBuffer& buffer = currentBuffer();
    if (buffer.changed) {
        if (buffer.is_new_file) {
            SaveFileBrowser();
            if (buffer.is_new_file) {
                msgwin("Save file before compiling.");
                return;
            }
        } else {
            write_file(buffer);
        }
    }

    // Save the current view state before showing the compile window
    m_pre_compile_view_state.line_num = buffer.current_line_num;
    m_pre_compile_view_state.col = buffer.cursor_col;
    int fv_linenum = 1;
    Line* p = buffer.document_head;
    while(p && p != buffer.first_visible_line) { p = p->next; fv_linenum++; }
    m_pre_compile_view_state.first_visible_line_num = fv_linenum;


    std::string cguess_cmd = "python3 cguess.py \"" + buffer.filename + "\" 2>/dev/null";
    std::string compile_cmd;
    FILE* pipe = popen(cguess_cmd.c_str(), "r");
    if (!pipe) {
        msgwin("Error: Could not run cguess.py script.");
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), pipe) != NULL) {
        if (strstr(line, "g++") != NULL || strstr(line, "clang++") != NULL) {
            compile_cmd = line;
            break;
        }
    }
    pclose(pipe);

    compile_cmd.erase(std::remove(compile_cmd.begin(), compile_cmd.end(), '\n'), compile_cmd.end());
    compile_cmd.erase(std::remove(compile_cmd.begin(), compile_cmd.end(), '\r'), compile_cmd.end());

    if (compile_cmd.empty()) {
        msgwin("cguess.py failed to produce a command.");
        return;
    }

    std::string temp_output_file = "tedit_compile_output.tmp";
    std::string full_compile_cmd = "(" + compile_cmd + ") > " + temp_output_file + " 2>&1";
    int compile_result = system(full_compile_cmd.c_str());

    std::ifstream output_stream(temp_output_file);
    std::string captured_output((std::istreambuf_iterator<char>(output_stream)), std::istreambuf_iterator<char>());
    output_stream.close();
    remove(temp_output_file.c_str());

    m_compile_output_lines.clear();
    std::istringstream stream(captured_output);
    std::string output_line;
    m_compile_output_lines.push_back({ "Command: " + compile_cmd, CMSG_NONE });
    m_compile_output_lines.push_back({ "", CMSG_NONE });

    // Regex to parse "filename:line:col: message"
    std::regex re_error_loc(R"(([^:]+):(\d+):(\d+):\s+(.+))");

    while (std::getline(stream, output_line)) {
        std::smatch match;

        // Only process lines that match the compiler error format
        if (std::regex_search(output_line, match, re_error_loc)) {
            CompileMessage msg;

            // Extract and shorten the filename
            std::string full_path = match[1];
            size_t last_slash = full_path.find_last_of('/');
            std::string basename = (last_slash == std::string::npos) ? full_path : full_path.substr(last_slash + 1);

            // Reconstruct the message with the short filename
            msg.full_text = basename + ":" + match[2].str() + ":" + match[3].str() + ": " + match[4].str();

            std::string lower_line = msg.full_text;
            std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);

            if (lower_line.find("error:") != std::string::npos) {
                msg.type = CMSG_ERROR;
            } else if (lower_line.find("warning:") != std::string::npos) {
                msg.type = CMSG_WARNING;
            }

            try {
                msg.line = std::stoi(match[2]);
                msg.col = std::stoi(match[3]);
            } catch (...) {
                msg.line = -1;
                msg.col = -1;
            }
            m_compile_output_lines.push_back(msg);
        } else if (!output_line.empty() && !std::isspace(output_line[0])) {
            // Add other non-context lines (like "compilation terminated.")
            m_compile_output_lines.push_back({ output_line, CMSG_NONE });
        }
    }

    if (compile_result == 0) {
        m_compile_output_lines.push_back({ "", CMSG_NONE });
        m_compile_output_lines.push_back({ "--- Compilation Successful ---", CMSG_NONE });
    }

    m_compile_output_scroll_pos = 0;
    m_compile_output_cursor_pos = 0;
    // Move cursor to first actual message
    while(m_compile_output_cursor_pos < (int)m_compile_output_lines.size() - 1 && m_compile_output_lines[m_compile_output_cursor_pos].type == CMSG_NONE) {
        m_compile_output_cursor_pos++;
    }
    m_compile_output_visible = true;
    m_renderer->hideCursor();
}

void TextEditor::drawCompileOutputWindow() {
    // --- New Layout Calculation ---
    int h = m_renderer->getHeight() / 4;
    if (h < 5) h = 5; // Ensure a minimum height
    int w = m_text_area_end_x - m_text_area_start_x + 4; // Extend width to the edge
    int starty = m_renderer->getHeight() - h - 1; // Move one row up
    int startx = m_text_area_start_x - 1;

    // --- Draw UI Elements ---
    m_renderer->drawBox(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::BoxStyle::DOUBLE);

    // Draw the background
    wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
    for (int i = 1; i < h - 1; ++i) {
        mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
    }
    wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

    // --- Draw Content and Handle Scrolling ---
    int text_height = h - 2;

    // Adjust scroll position to keep the cursor in view
    if (m_compile_output_cursor_pos < m_compile_output_scroll_pos) {
        m_compile_output_scroll_pos = m_compile_output_cursor_pos;
    }
    if (m_compile_output_cursor_pos >= m_compile_output_scroll_pos + text_height) {
        m_compile_output_scroll_pos = m_compile_output_cursor_pos - text_height + 1;
    }

    // Draw the scrollable text content
    for (int i = 0; i < text_height; ++i) {
        int line_idx = m_compile_output_scroll_pos + i;
        if (line_idx < (int)m_compile_output_lines.size()) {
            const auto& msg = m_compile_output_lines[line_idx];
            std::string line_to_draw = msg.full_text;

            int color = Renderer::CP_DIALOG;
            switch(msg.type) {
            case CMSG_ERROR: color = Renderer::CP_COMPILE_ERROR; break;
            case CMSG_WARNING: color = Renderer::CP_COMPILE_WARNING; break;
            default: break;
            }

            // Highlight the selected line
            if (line_idx == m_compile_output_cursor_pos) {
                // Draw a full-width highlight bar
                wattron(stdscr, COLOR_PAIR(Renderer::CP_HIGHLIGHT));
                mvwaddstr(stdscr, starty + 1 + i, startx + 1, std::string(w - 2, ' ').c_str());
                wattroff(stdscr, COLOR_PAIR(Renderer::CP_HIGHLIGHT));
            }

            if (line_to_draw.length() > (size_t)w - 4) {
                line_to_draw = line_to_draw.substr(0, w - 4);
            }

            // Use highlight color for text on the selected line
            int text_color = (line_idx == m_compile_output_cursor_pos) ? Renderer::CP_HIGHLIGHT : color;
            m_renderer->drawText(startx + 2, starty + 1 + i, line_to_draw, text_color);
        }
    }

    // Draw scrollbar indicators if needed
    if (m_compile_output_lines.size() > (size_t)text_height) {
        if (m_compile_output_scroll_pos > 0) {
            m_renderer->drawText(startx + w - 2, starty, "↑", Renderer::CP_HIGHLIGHT);
        }
        if (m_compile_output_scroll_pos + text_height < (int)m_compile_output_lines.size()) {
            m_renderer->drawText(startx + w - 2, starty + h - 1, "↓", Renderer::CP_HIGHLIGHT);
        }
    }
}

void TextEditor::ActivateReplace() {
    if (m_current_buffer_idx == -1) return;

    m_renderer->hideCursor();

    int h = 10, w = 55;
    int starty = (m_renderer->getHeight() - h) / 2;
    int startx = (m_renderer->getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

    std::string find_buf = m_search_term;
    std::string replace_buf = m_replace_term;

    int focus = 0; // 0: find, 1: replace, 2: Replace Btn, 3: Replace All Btn, 4: Cancel Btn
    nodelay(stdscr, FALSE);

    while (true) {
        // Draw UI
        m_renderer->drawShadow(startx, starty, w, h);
        m_renderer->drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::SINGLE, " Replace ", Renderer::CP_DIALOG_TITLE, A_BOLD);
        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        for (int i = 1; i < h - 1; ++i) {
            mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
        }
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        // Labels
        m_renderer->drawText(startx + 3, starty + 2, "Find what:", Renderer::CP_DIALOG);
        m_renderer->drawText(startx + 3, starty + 4, "Replace with:", Renderer::CP_DIALOG);

        // Input fields
        m_renderer->drawText(startx + 17, starty + 2, std::string(w - 20, ' '), Renderer::CP_LIST_BOX);
        m_renderer->drawText(startx + 17, starty + 4, std::string(w - 20, ' '), Renderer::CP_LIST_BOX);
        m_renderer->drawText(startx + 18, starty + 2, find_buf, Renderer::CP_LIST_BOX);
        m_renderer->drawText(startx + 18, starty + 4, replace_buf, Renderer::CP_LIST_BOX);

        // Buttons
        m_renderer->drawText(startx + 5, starty + 7, " Replace ", focus == 2 ? Renderer::CP_DIALOG_BUTTON : Renderer::CP_DIALOG);
        m_renderer->drawText(startx + 20, starty + 7, " Replace All ", focus == 3 ? Renderer::CP_DIALOG_BUTTON : Renderer::CP_DIALOG);
        m_renderer->drawText(startx + w - 12, starty + 7, " Cancel ", focus == 4 ? Renderer::CP_DIALOG_BUTTON : Renderer::CP_DIALOG);

        // Set cursor position
        if (focus == 0) {
            move(starty + 2, startx + 18 + find_buf.length());
        } else if (focus == 1) {
            move(starty + 4, startx + 18 + replace_buf.length());
        }
        m_renderer->showCursor();
        m_renderer->refresh();

        wint_t ch = m_renderer->getChar();

        switch (ch) {
        case 9: // Tab
            focus = (focus + 1) % 5;
            break;
        case KEY_UP:
            if (focus == 1) focus = 0;
            break;
        case KEY_DOWN:
            if (focus == 0) focus = 1;
            break;
        case KEY_LEFT:
            if (focus > 2) focus--;
            break;
        case KEY_RIGHT:
            if (focus >= 2 && focus < 4) focus++;
            break;
        case 27: // ESC
            goto end_replace_dialog;
        case KEY_BACKSPACE: case 127: case 8:
            if (focus == 0 && !find_buf.empty()) find_buf.pop_back();
            if (focus == 1 && !replace_buf.empty()) replace_buf.pop_back();
            break;
        case KEY_ENTER: case 10: case 13:
            if (focus == 2) { // Replace
                m_search_term = find_buf;
                m_replace_term = replace_buf;
                PerformReplace();
                goto end_replace_dialog;
            }
            if (focus == 3) { // Replace All
                m_search_term = find_buf;
                m_replace_term = replace_buf;
                PerformReplaceAll();
                goto end_replace_dialog;
            }
            if (focus == 4) { // Cancel
                goto end_replace_dialog;
            }
            break;
        default:
            if (ch > 31 && ch < KEY_MIN) {
                std::string utf8_char = wchar_to_utf8(ch);
                if (focus == 0) find_buf += utf8_char;
                if (focus == 1) replace_buf += utf8_char;
            }
            break;
        }
    }

end_replace_dialog:
    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE); delwin(behind);
    nodelay(stdscr, TRUE);
    handleResize();
}

void TextEditor::PerformReplace() {
    if (m_search_term.empty()) {
        PerformSearch(false); // Just find if search term is empty but replace isn't
        return;
    }

    // Check if the current selection actually matches the search term
    if (currentBuffer().selecting) {
        std::string selected_text;
        Line* p = currentBuffer().selection_anchor_line;
        int start_col = currentBuffer().selection_anchor_col;
        int end_col = currentBuffer().cursor_col;

        // This simple version only works for single-line selections
        if (p == currentBuffer().current_line) {
            if (start_col > end_col) std::swap(start_col, end_col);
            selected_text = p->text.substr(start_col - 1, end_col - start_col);
        }

        std::string lower_selected = selected_text;
        std::string lower_search = m_search_term;
        std::transform(lower_selected.begin(), lower_selected.end(), lower_selected.begin(), ::tolower);
        std::transform(lower_search.begin(), lower_search.end(), lower_search.begin(), ::tolower);

        if (lower_selected == lower_search) {
            // It's a match, perform replacement
            DeleteSelection();
            currentBuffer().current_line->text.insert(currentBuffer().cursor_col - 1, m_replace_term);
            currentBuffer().cursor_col += m_replace_term.length();
            currentBuffer().changed = true;
            CreateUndoPoint(currentBuffer());
        }
    }

    // Find the next occurrence
    PerformSearch(true);
}

void TextEditor::PerformReplaceAll() {
    if (m_search_term.empty()) return;
    CreateUndoPoint(currentBuffer());

    int replacements = 0;
    std::string lower_search = m_search_term;
    std::transform(lower_search.begin(), lower_search.end(), lower_search.begin(), ::tolower);

    // Save original cursor position to restore it later
    int original_line_num = currentBuffer().current_line_num;
    int original_col = currentBuffer().cursor_col;

    for (Line* p = currentBuffer().document_head; p != nullptr; p = p->next) {
        std::string lower_line = p->text;
        std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);

        size_t pos = lower_line.find(lower_search);
        while (pos != std::string::npos) {
            p->text.replace(pos, m_search_term.length(), m_replace_term);
            replacements++;

            // Update the line for the next search to avoid infinite loops
            lower_line = p->text;
            std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);
            pos = lower_line.find(lower_search, pos + m_replace_term.length());
        }
    }

    if (replacements > 0) {
        currentBuffer().changed = true;
    }

    // Restore cursor to its original line
    currentBuffer().current_line_num = original_line_num;
    currentBuffer().cursor_col = original_col;
    currentBuffer().current_line = currentBuffer().document_head;
    for(int i = 1; i < currentBuffer().current_line_num; ++i) {
        if(currentBuffer().current_line->next) currentBuffer().current_line = currentBuffer().current_line->next;
    }

    msgwin("Replaced " + std::to_string(replacements) + " occurrence(s).");
    update_cursor_and_scroll();
}
