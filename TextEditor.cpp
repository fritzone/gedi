#include "TextEditor.h"
#include "SyntaxHighlighter.h"
#include "FileBrowser.h"
#include "ComboBox.h"
#include "TabControl.h"
#include "utils.h"

#include <ncurses.h>

#include <filesystem>
#include <fstream>
#include <thread>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <cstdio>
#include <regex>
#include <algorithm>
#include <sstream>


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

void TextEditor::OpenFileBrowser() {
    std::string filename = FileBrowser::open(*m_renderer);
    if (!filename.empty()) {
        for(size_t i = 0; i < m_bufferManager->bufferCount(); ++i) {
            if (m_bufferManager->getBuffer(i).filename == filename) {
                SwitchToBuffer(i);
                handleResize();
                return;
            }
        }
        DoNew();
        currentBuffer().filename = filename;
        read_file(currentBuffer());
        handleResize();
    }
}

void TextEditor::SaveFileBrowser() {
    if (currentBufferIdx() == -1) return;
    EditorBuffer& buffer = currentBuffer();
    std::string filename = FileBrowser::save(*m_renderer, buffer.filename);
    if (!filename.empty()) {
        buffer.filename = filename;
        write_file(buffer);
        SyntaxHighlighter::setSyntaxType(buffer);
        handleResize();
    }
}

void TextEditor::selectfile() {
    OpenFileBrowser();
}


void TextEditor::run(int argc, char* argv[]) {
    m_renderer = std::make_unique<Renderer>();
    
    std::string configPath = "config.json";
    std::string colorsPath = "colors.json";
    
    if (!std::filesystem::exists(configPath)) configPath = "/usr/share/gedi/config.json";
    if (!std::filesystem::exists(colorsPath)) colorsPath = "/usr/share/gedi/colors.json";

    m_configManager = std::make_unique<ConfigManager>(configPath, colorsPath);
    m_configManager->loadConfig(m_config);
    m_themes_data = m_configManager->loadThemes();

    m_keyBindings = std::make_unique<KeyBindings>();
    m_keyBindings->loadFromConfig(m_config.keybindings);

    m_buildSystem = std::make_unique<BuildSystem>(m_config);
    m_helpProvider = std::make_unique<HelpProvider>();
    m_bufferManager = std::make_unique<BufferManager>();

    loadHelpFile();
    updateMenuLabels();

    if (m_themes_data.contains(m_config.color_scheme_name)) {
        m_renderer->loadColors(m_themes_data[m_config.color_scheme_name]);
    } else {
        msgwin("Theme not found, using first available.");
        if (!m_themes_data.empty()) {
            m_renderer->loadColors(m_themes_data.begin().value());
        }
    }

    if (argc < 2) {
        DoNew();
    } else {
        m_bufferManager->addBuffer();
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
    SyntaxHighlighter::setSyntaxType(buffer);
}

void TextEditor::write_file(EditorBuffer& buffer) {
    std::ofstream f(buffer.filename);
    if (!f.is_open()) { msgwin("Error: Cannot write to file " + buffer.filename); return; }
    for (Line* p = buffer.document_head; p != nullptr; p = p->next) { f << p->text << std::endl; }
    f.close();
    buffer.changed = false;
    buffer.is_new_file = false;

    // Invalidate the compile command cache for this file, as its content has changed.
    m_buildSystem->invalidateCache(buffer.filename);
}

void TextEditor::TryExit() {
    for (size_t i = 0; i < m_bufferManager->bufferCount(); ++i) {
        EditorBuffer& buffer = m_bufferManager->getBuffer(i);
        if (buffer.changed) {
            SwitchToBuffer(i);
            drawEditorState();
            int res = msgwin_yesno("Save changes to:", buffer.filename);
            if (res == 1) { // Yes
                if (buffer.is_new_file) SaveFileBrowser();
                else write_file(buffer);
                
                // If they cancelled the save or it failed, don't exit
                if (buffer.changed) return;
            } else if (res == 0) { // No
                // Continue to next buffer without saving
            } else if (res == -1) { // Cancel/ESC
                return; 
            }
        }
    }
    main_loop_running = false;
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

    if (currentBufferIdx() != -1) {
        std::string filename_part = " " + currentBuffer().filename + " ";
        filename_part = get_full_path(filename_part);
        std::string indicator_part = "* ";

        std::string bufferNr = "[" + std::to_string(currentBuffer().bufferNr) + "]";

        int total_len = filename_part.length() + (currentBuffer().changed ? indicator_part.length() : 0);
        int title_x = box_x + (box_w - total_len) / 2;

        int current_x = title_x;
        if (currentBuffer().changed) {
            m_renderer->drawText(current_x, box_y, indicator_part, Renderer::CP_CHANGED_INDICATOR, A_BOLD);
            current_x += indicator_part.length();
        }
        m_renderer->drawText(current_x, box_y, filename_part, Renderer::CP_HIGHLIGHT);
        m_renderer->drawText(box_w - 4, box_y, bufferNr, Renderer::CP_CHANGED_INDICATOR);

    }
}

void TextEditor::drawTextArea() {
    if (currentBufferIdx() == -1) return;
    EditorBuffer& buffer = currentBuffer();

    buffer.in_multiline_comment = false;
    Line* p_find_comment = buffer.document_head;
    for (int i=1; i < buffer.current_line_num && p_find_comment != buffer.first_visible_line; ++i) {
        SyntaxHighlighter::parseLine(buffer, p_find_comment->text, *m_renderer);
        if (p_find_comment->next) p_find_comment = p_find_comment->next;
        else break;
    }

    Line* p = buffer.first_visible_line;
    int text_area_height = m_text_area_end_y - m_text_area_start_y + 1;
    int text_area_width = m_text_area_end_x - m_text_area_start_x + 1 - m_gutter_width;
    if (text_area_height <= 0 || text_area_width <= 0) return;

    int current_doc_line = 0;
    Line* temp = buffer.document_head;
    while(temp && temp != buffer.first_visible_line) {
        current_doc_line++;
        temp = temp->next;
    }

    for(int i = 0; i < text_area_height; ++i) {
        int current_screen_y = m_text_area_start_y + i;

        m_renderer->drawText(m_text_area_start_x, current_screen_y, std::string(m_gutter_width + text_area_width, ' '), Renderer::CP_DEFAULT_TEXT);

        if (m_gutter_width > 0) {
            m_renderer->drawText(m_text_area_start_x, current_screen_y, std::string(m_gutter_width, ' '), Renderer::CP_GUTTER_BG);
            m_renderer->drawText(m_text_area_start_x + m_gutter_width - 1, current_screen_y, "│", Renderer::CP_GUTTER_BG);
        }

        if (p != nullptr) {
            if (m_gutter_width > 0) {
                std::string line_num_str = std::to_string(current_doc_line + i + 1);
                m_renderer->drawText(m_text_area_start_x + m_gutter_width - line_num_str.length() - 1, current_screen_y, line_num_str, Renderer::CP_GUTTER_FG);
            }

            std::vector<SyntaxToken> tokens;
            if (buffer.syntax_type != EditorBuffer::ST_NONE) {
                tokens = SyntaxHighlighter::parseLine(buffer, p->text, *m_renderer);
            }

            int screen_x = m_text_area_start_x + m_gutter_width;
            int token_idx = 0;
            size_t token_char_offset = 0;

            for (size_t char_idx = 0; char_idx < p->text.length(); ++char_idx) {
                int current_col = char_idx + 1;
                if (current_col >= buffer.horizontal_scroll_offset) {
                    if (screen_x > m_text_area_end_x) break;

                    bool is_char_selected = p->selected && (current_col >= p->selection_start_col && current_col < p->selection_end_col);

                    int color = Renderer::CP_DEFAULT_TEXT;
                    int flags = 0;

                    if (is_char_selected) {
                        color = Renderer::CP_SELECTION;
                    } else {
                        if (buffer.syntax_type != EditorBuffer::ST_NONE) {
                            while (token_idx < tokens.size() && token_char_offset + tokens[token_idx].text.length() <= char_idx) {
                                token_char_offset += tokens[token_idx].text.length();
                                token_idx++;
                            }
                            if (token_idx < tokens.size()) {
                                color = tokens[token_idx].colorId;
                                flags = tokens[token_idx].flags;
                            }
                        }
                    }
                    m_renderer->drawText(screen_x, current_screen_y, std::string(1, p->text[char_idx]), color, flags);
                    screen_x++;
                }
            }
            p = p->next;
        }
    }
}

void TextEditor::drawMenuBar(int active_menu_id) {
    int w = m_renderer->getWidth();
    m_renderer->drawText(0, 0, std::string(w, ' '), Renderer::CP_MENU_BAR);
    for (size_t i = 0; i < m_menus.size(); ++i) {
        int menu_id = i + 1;
        int x_pos = m_menu_positions[i];
        
        // If it is the last menu (Help), move it to the right
        if (i == m_menus.size() - 1) {
            x_pos = w - m_menus[i].length() - 2;
        }

        if (x_pos + m_menus[i].length() > (size_t)w) continue;
        
        bool is_active = (menu_id == active_menu_id);
        int bar_color = is_active ? Renderer::CP_MENU_SELECTED : Renderer::CP_MENU_BAR;
        m_renderer->drawStyledText(x_pos, 0, m_menus[i], bar_color);
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

    if (currentBufferIdx() != -1) {
        EditorBuffer& buffer = currentBuffer();
        char status_buf[120];
        snprintf(status_buf, sizeof(status_buf), "Line: %-5d Col: %-5d %s", buffer.current_line_num, buffer.cursor_col, (buffer.insert_mode ? "INS" : "OVR"));
        if (w > 50 + (int)strlen(status_buf)) {
            m_renderer->drawText(w - strlen(status_buf) - 2, h - 1, status_buf, Renderer::CP_STATUS_BAR);
        }
    }
}

void TextEditor::drawScrollbars() {
    if (m_renderer->getWidth() < 5 || m_renderer->getHeight() < 5 || currentBufferIdx() == -1) return;
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

int TextEditor::msgwin_yesno(const std::string& question, const std::string& info) {
    return QuestionDialog::ask(*m_renderer, question, info);
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

std::string TextEditor::formatMenuItem(const std::string& label, EditorAction action, int width) {
    std::string key_label = m_keyBindings->getLabel(action);
    if (key_label.empty()) return " " + label + std::string(width - label.length() - 2, ' ') + " ";
    
    int padding = width - label.length() - key_label.length() - 2;
    if (padding < 1) padding = 1;
    return " " + label + std::string(padding, ' ') + key_label + " ";
}

void TextEditor::updateMenuLabels() {
    m_menus = {" &File " , " &Edit " , " &Search ", " &Build " , " &Window ", " &Options ", " &Help "  };
    m_menu_positions = { 1, 7, 13, 21, 28, 36, 45 };

    m_submenu_file = {
        formatMenuItem("&New", ACT_NEW),
        formatMenuItem("&Open...", ACT_OPEN),
        " -------------- ",
        formatMenuItem("&Save", ACT_SAVE),
        formatMenuItem("Save &As...", ACT_SAVE_AS),
        " -------------- ",
        formatMenuItem("E&xit", ACT_EXIT)
    };

    m_submenu_edit = {
        formatMenuItem("&Undo", ACT_UNDO),
        formatMenuItem("&Redo", ACT_REDO),
        " -------------- ",
        formatMenuItem("Cu&t", ACT_CUT),
        formatMenuItem("&Copy", ACT_COPY),
        formatMenuItem("&Paste", ACT_PASTE),
        formatMenuItem("&Delete", ACT_DELETE),
        " -------------- ",
        formatMenuItem("Comment Line", ACT_TOGGLE_COMMENT),
        formatMenuItem("Uncomment Line", ACT_TOGGLE_COMMENT)
    };

    m_submenu_search = {
        formatMenuItem("&Find...", ACT_FIND),
        " Find &Next",
        " Find Pre&vious",
        formatMenuItem("&Replace...", ACT_REPLACE),
        " -------------- ",
        formatMenuItem("&Go To Line...", ACT_GOTO_LINE)
    };

    m_submenu_build = {
        formatMenuItem("&Run", ACT_RUN),
        formatMenuItem("&Compile", ACT_COMPILE),
        formatMenuItem("Compile &Options...", ACT_COMPILE_OPTIONS)
    };

    m_submenu_window = {
        formatMenuItem("&Output Screen", ACT_TOGGLE_OUTPUT),
        " -------------- ",
        formatMenuItem("&Next Window", ACT_NEXT_BUFFER),
        formatMenuItem("&Previous Window", ACT_PREV_BUFFER),
        formatMenuItem("&Close Window", ACT_CLOSE_BUFFER)
    };

    m_submenu_options = { formatMenuItem("Editor &Settings...", ACT_SETTINGS) };

    m_submenu_help = {
        formatMenuItem("&View Help...", ACT_HELP),
        formatMenuItem("&About...", ACT_ABOUT)
    };
}

void TextEditor::main_loop() {
    main_loop_running = true;
    while (main_loop_running) {
        if (m_output_screen_visible) {
            showOutputScreen();
            continue;
        }

        // Calculate gutter width at the start of the loop
        if (m_config.show_line_numbers && currentBufferIdx() != -1) {
            m_gutter_width = std::to_string(currentBuffer().total_lines).length() + 2;
        } else {
            m_gutter_width = 0;
        }

        update_cursor_and_scroll();
        drawEditorState();
        if (currentBufferIdx() != -1) {
            if (m_search_mode) {
                m_renderer->setCursor(1 + strlen("Search: ") + m_search_term.length(), m_renderer->getHeight() - 1);
            } else if (!m_compile_output_visible) {
                EditorBuffer& buffer = currentBuffer();
                // Adjust cursor position for the gutter
                m_renderer->setCursor(buffer.cursor_col - buffer.horizontal_scroll_offset + m_text_area_start_x + m_gutter_width, buffer.cursor_screen_y);
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
                        if (m_compile_output_lines[new_pos].type != CompileMessage::CMSG_NONE) {
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
                        if (m_compile_output_lines[new_pos].type != CompileMessage::CMSG_NONE) {
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
    if (currentBufferIdx() == -1) return;
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

void TextEditor::HandleAltKey(wint_t key) {
    wint_t lookup_key = key;
    if (key >= 'a' && key <= 'z') lookup_key = toupper(key);
    
    EditorAction action = m_keyBindings->getAction(KEY_ALT(lookup_key));
    if (action != ACT_UNKNOWN) {
        process_key(KEY_ALT(lookup_key)); // Dispatch via standard action mechanism
        return;
    }

    switch (tolower(key)) {
    case 'f': ActivateMenuBar(1); break; // File
    case 'e': ActivateMenuBar(2); break; // Edit
    case 's': ActivateMenuBar(3); break; // Search
    case 'b': ActivateMenuBar(4); break; // Build (was 5)
    case 'w': ActivateMenuBar(5); break; // Window (was 6)
    case 'o': ActivateMenuBar(6); break; // Options (was 7)
    case 'h': ActivateMenuBar(7); break; // Help (was 8)
    case 'y': HandleRedo(); break;
    case KEY_BACKSPACE: HandleUndo(); break;
    case 'c': CloseWindow(); break;
    case '1': if (m_bufferManager->bufferCount() >= 1) SwitchToBuffer(0); break;
    case '2': if (m_bufferManager->bufferCount() >= 2) SwitchToBuffer(1); break;
    case '3': if (m_bufferManager->bufferCount() >= 3) SwitchToBuffer(2); break;
    case '4': if (m_bufferManager->bufferCount() >= 4) SwitchToBuffer(3); break;
    case '5': if (m_bufferManager->bufferCount() >= 5) SwitchToBuffer(4); break;
    case '6': if (m_bufferManager->bufferCount() >= 6) SwitchToBuffer(5); break;
    case '7': if (m_bufferManager->bufferCount() >= 7) SwitchToBuffer(6); break;
    case '8': if (m_bufferManager->bufferCount() >= 8) SwitchToBuffer(7); break;
    case '9': if (m_bufferManager->bufferCount() >= 9) SwitchToBuffer(8); break;
    case '0': if (m_bufferManager->bufferCount() >= 10) SwitchToBuffer(9); break;
    }
}

void TextEditor::ClearSelection() {
    if (currentBufferIdx() == -1) return;
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
    if (currentBufferIdx() == -1) return;
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
    if (currentBufferIdx() == -1 || !currentBuffer().selecting) return;
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
    if (currentBufferIdx() == -1 || !currentBuffer().selecting) return;
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
    if (currentBufferIdx() == -1 || !currentBuffer().selecting) return;
    HandleCopy();
    DeleteSelection();
}

void TextEditor::HandlePaste() {
    if (currentBufferIdx() == -1) return;
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
    if (currentBufferIdx() == -1 || currentBuffer().undo_stack.empty()) return;

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
    if (currentBufferIdx() == -1 || currentBuffer().redo_stack.empty()) return;

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
    if (currentBufferIdx() == -1) return;
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
    if (currentBufferIdx() == -1) return;
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
    if (currentBufferIdx() == -1 || !currentBuffer().current_line->next) return;
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
    if (currentBufferIdx() == -1 || !currentBuffer().current_line->prev) return;
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

void TextEditor::handleSmartBlockClose(wint_t closing_char) {
    EditorBuffer& buffer = currentBuffer();

    char open_char;
    switch (closing_char) {
    case ')': open_char = '('; break;
    case ']': open_char = '['; break;
    case '}': open_char = '{'; break;
    default: return; // Should not happen
    }

    int nesting_level = 0;
    Line* search_line = buffer.current_line;
    int search_col = buffer.cursor_col - 1;

    while (search_line != nullptr) {
        const std::string& line_text = search_line->text;
        for (int i = search_col; i >= 0; --i) {
            if (i < (int)line_text.length()) {
                if (line_text[i] == closing_char) {
                    nesting_level++;
                } else if (line_text[i] == open_char) {
                    nesting_level--;
                    if (nesting_level < 0) {
                        // Found the matching brace
                        size_t indent_pos = search_line->text.find_first_not_of(" \t");
                        std::string indent_str = (indent_pos != std::string::npos) ? search_line->text.substr(0, indent_pos) : "";

                        // Check if the current line is only whitespace
                        size_t current_line_char_pos = buffer.current_line->text.find_first_not_of(" \t");
                        if (current_line_char_pos == std::string::npos) {
                            buffer.current_line->text = indent_str + wchar_to_utf8(closing_char);
                            buffer.cursor_col = indent_str.length() + 2;
                        } else {
                            // Line is not empty, just insert the character
                            buffer.current_line->text.insert(buffer.cursor_col - 1, wchar_to_utf8(closing_char));
                            buffer.cursor_col++;
                        }
                        buffer.changed = true;
                        return;
                    }
                }
            }
        }
        search_line = search_line->prev;
        if (search_line) {
            search_col = search_line->text.length() - 1;
        }
    }

    // No matching brace found, just insert the character normally
    buffer.current_line->text.insert(buffer.cursor_col - 1, wchar_to_utf8(closing_char));
    buffer.cursor_col++;
    buffer.changed = true;
}


void TextEditor::process_key(wint_t ch) {
    if (currentBufferIdx() != -1) {
        EditorAction action = m_keyBindings->getAction(ch);
        if (action != ACT_UNKNOWN) {
            switch (action) {
                case ACT_NEW: DoNew(); return;
                case ACT_OPEN: selectfile(); return;
                case ACT_SAVE: if (currentBuffer().is_new_file) SaveFileBrowser(); else write_file(currentBuffer()); return;
                case ACT_SAVE_AS: SaveFileBrowser(); return;
                case ACT_EXIT: TryExit(); return;
                case ACT_UNDO: HandleUndo(); return;
                case ACT_REDO: HandleRedo(); return;
                case ACT_CUT: HandleCut(); return;
                case ACT_COPY: HandleCopy(); return;
                case ACT_PASTE: HandlePaste(); return;
                case ACT_FIND: ActivateSearch(); return;
                case ACT_REPLACE: ActivateReplace(); return;
                case ACT_GOTO_LINE: GoToLineDialog(); return;
                case ACT_COMPILE: compileOnly(); return;
                case ACT_RUN: compileAndRun(); return;
                case ACT_TOGGLE_OUTPUT: showOutputScreen(); return;
                case ACT_NEXT_BUFFER: NextWindow(); return;
                case ACT_PREV_BUFFER: PreviousWindow(); return;
                case ACT_CLOSE_BUFFER: CloseWindow(); return;
                case ACT_SETTINGS: EditorSettingsDialog(); return;
                case ACT_HELP: showHelpDialog(); return;
                case ACT_ABOUT: AboutBox(); return;
                case ACT_TOGGLE_COMMENT: handleToggleComment(); return;
                case ACT_DELETE: DeleteSelection(); return;
                default: break;
            }
        }
    }

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
        if (currentBufferIdx() != -1) {
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
    case 31: // Ctrl+/ (ASCII Unit Separator), a common mapping for this key combo
        handleToggleComment();
        return;
    }

    if (ch == KEY_F(10)) { ActivateMenuBar(1); return; }
    if (ch == 27) { // ESC key
        if (currentBufferIdx() != -1 && currentBuffer().selecting) {
            ClearSelection();
        } else {
            nodelay(stdscr, FALSE);
            timeout(50);
            wint_t next_ch = m_renderer->getChar();
            timeout(-1);
            nodelay(stdscr, TRUE);

            if (next_ch != ERR) {
                HandleAltKey(next_ch);
            }
        }
        return;
    }
    if (ch >= 128 && ch < 256) { char base_char = tolower(ch & 0x7F); if (base_char == 'f' || base_char == 'e' || base_char == 's' || base_char == 'v' || base_char == 'b' || base_char == 'w' || base_char == 'o' || base_char == 'h' || base_char == 'x') { HandleAltKey(base_char); return; } }

    if (currentBufferIdx() == -1) return;

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

        if (first_char_pos != std::string::npos && cursor_idx < (int)first_char_pos)
        {
            buffer.cursor_col = first_char_pos + 1;
        }
        else
        {
            std::string spaces_to_insert(m_config.indentation_width, ' ');

            if (cursor_idx > (int)line_text.length()) {
                cursor_idx = line_text.length();
            }

            buffer.current_line->text.insert(cursor_idx, spaces_to_insert);
            buffer.cursor_col += m_config.indentation_width;
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

        if (m_config.smart_indentation) {
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
                indent_str += std::string(m_config.indentation_width, ' ');
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
            if (buffer.selecting) {
                DeleteSelection();
            }
            // Check for smart brace closing
            if (ch == ')' || ch == ']' || ch == '}') {
                handleSmartBlockClose(ch);
            } else {
                // Standard character insertion
                std::string utf8_char = wchar_to_utf8(ch);
                if (currentBuffer().insert_mode) {
                    currentBuffer().current_line->text.insert(currentBuffer().cursor_col - 1, utf8_char);
                } else {
                    if (currentBuffer().cursor_col <= (int)currentBuffer().current_line->text.length()) {
                        currentBuffer().current_line->text.replace(currentBuffer().cursor_col - 1, 1, utf8_char);
                    } else {
                        currentBuffer().current_line->text += utf8_char;
                    }
                }
                currentBuffer().cursor_col++;
                currentBuffer().changed = true;
            }
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

    m_bufferManager->addBuffer();

    // Now, override the default filename and call read_file to correctly initialize it as empty
    currentBuffer().filename = filename;
    currentBuffer().is_new_file = true; // Make sure it's marked as new
    read_file(currentBuffer()); // This will handle creating the empty line and setting syntax
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

    int help_x = w - m_menus.back().length() - 3;

    MenuAction action;
    std::map<int, std::pair<const std::vector<std::string>*, int>> menus_by_id = {
        {1, {&m_submenu_file, m_menu_positions[0] - 1}}, {2, {&m_submenu_edit, m_menu_positions[1] - 1}},
        {3, {&m_submenu_search, m_menu_positions[2] - 1}}, {4, {&m_submenu_build, m_menu_positions[3] - 1}},
        {5, {&m_submenu_window, m_menu_positions[4] - 1}}, {6, {&m_submenu_options, m_menu_positions[5] - 1}},
        {7, {&m_submenu_help, help_x}}
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
    if (menu_id == 5) { // Window menu
        finalMenuItems.push_back(" ----------------- ");
        for(size_t i = 0; i < m_bufferManager->bufferCount() && i < 10; ++i) {
            std::string hotkey_num = (i < 9) ? std::to_string(i + 1) : "0";
            std::string filename_to_display = get_filename_from_path(get_full_path(m_bufferManager->getBuffer(i).filename));
            std::string text_part = " &" + hotkey_num + " " + filename_to_display;
            std::string hotkey_part = "Alt+" + hotkey_num;
            const int total_width = 28;
            if (text_part.length() + hotkey_part.length() + 1 > total_width) {
                int available_len = total_width - hotkey_part.length() - 1 - 3;
                if (available_len < 5) available_len = 5;
                text_part = text_part.substr(0, available_len) + "...";
            }
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
    
    // Ensure menu fits on screen
    if (x + w > m_renderer->getWidth()) {
        x = m_renderer->getWidth() - w;
    }
    if (x < 0) x = 0;

    if (y + h > m_renderer->getHeight()) { return CLOSE_MENU; }

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
            // Re-numbered cases for menu logic
            switch (menu_id) {
            case 1: // File
                if (selection == 1) DoNew();
                else if (selection == 2) selectfile();
                else if (selection == 4) { if (currentBuffer().is_new_file) SaveFileBrowser(); else write_file(currentBuffer()); }
                else if (selection == 5) SaveFileBrowser();
                else if (selection == 7) TryExit();
                else NotImplemented();
                break;
            case 2: // Edit
                if (selection == 1) HandleUndo();
                else if (selection == 2) HandleRedo();
                else if (selection == 4) HandleCut();
                else if (selection == 5) HandleCopy();
                else if (selection == 6) HandlePaste();
                else if (selection == 7) DeleteSelection();
                else if (selection == 9) handleToggleComment();
                else if (selection == 10) handleToggleComment();
                else NotImplemented();
                break;
            case 3: // Search
                if (selection == 1) ActivateSearch();
                else if (selection == 4) ActivateReplace();
                else if (selection == 6) GoToLineDialog();
                break;
            case 4: // Build (was 5)
                if (selection == 1) compileAndRun();
                else if (selection == 2) compileOnly();
                else if (selection == 3) CompileOptionsDialog();
                break;
            case 5: // Window (was 6), updated selection logic
                if (selection == 1) { // New: Output Screen
                    m_output_screen_visible = !m_output_screen_visible;
                    if (!m_output_screen_visible) handleResize();
                }
                else if (selection == 3) NextWindow();
                else if (selection == 4) PreviousWindow();
                else if (selection == 5) CloseWindow();
                else if (selection > 6) { // File list starts after static items + 2 separators
                    int buffer_idx = selection - 7;
                    if (buffer_idx < (int)m_bufferManager->bufferCount()) SwitchToBuffer(buffer_idx);
                }
                break;
            case 6: // Options
                EditorSettingsDialog(); break;
            case 7: // Help
                if (selection == 1) showHelpDialog();
                else if (selection == 2) AboutBox();
                break;
            default: NotImplemented(); break;
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

void TextEditor::NextWindow() {
    if (m_bufferManager->bufferCount() > 1) {
        int next_idx = (currentBufferIdx() + 1) % m_bufferManager->bufferCount();
        m_bufferManager->setCurrentBufferIndex(next_idx);
    }
}

void TextEditor::PreviousWindow() {
    if (m_bufferManager->bufferCount() > 1) {
        int prev_idx = (currentBufferIdx() - 1 + m_bufferManager->bufferCount()) % m_bufferManager->bufferCount();
        m_bufferManager->setCurrentBufferIndex(prev_idx);
    }
}

void TextEditor::CloseWindow() {
    if (currentBufferIdx() == -1) return;

    if (currentBuffer().changed) {
        int result = msgwin_yesno("Save changes to file?", currentBuffer().filename);
        if (result == 1) {
            if (currentBuffer().is_new_file) SaveFileBrowser();
            else write_file(currentBuffer());
            if (currentBuffer().changed) return;
        } else if (result == -1) {
            return;
        }
    }

    m_bufferManager->removeBuffer(currentBufferIdx());

    if (!m_bufferManager->hasBuffers()) {
        DoNew();
    }
}

void TextEditor::SwitchToBuffer(int index) {
    m_bufferManager->setCurrentBufferIndex(index);
}

void TextEditor::ActivateSearch() {
    if (currentBufferIdx() == -1) return;

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
    if (m_search_term.empty() || currentBufferIdx() == -1) return;

    EditorBuffer& buffer = currentBuffer();
    SearchResult res = SearchEngine::search(buffer, m_search_term, buffer.current_line, buffer.current_line_num, next ? buffer.cursor_col : 0, true);

    if (res.found) {
        buffer.current_line = res.line;
        buffer.current_line_num = res.line_num;
        buffer.cursor_col = res.col;

        buffer.selecting = true;
        buffer.selection_anchor_line = res.line;
        buffer.selection_anchor_linenum = res.line_num;
        buffer.selection_anchor_col = res.col;
        buffer.cursor_col += m_search_term.length();
        UpdateSelection();

        update_cursor_and_scroll();
    }
}

// Config management is now handled by ConfigManager

void TextEditor::EditorSettingsDialog() {
    std::vector<std::string> themes;
    for (auto const& [key, val] : m_themes_data.items()) {
        themes.push_back(key);
    }
    std::sort(themes.begin(), themes.end());
    SettingsDialog::show(*m_renderer, m_config, *m_configManager, themes);
}

// Build command generation is now handled by BuildSystem

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


// --- New function to display a scrollable dialog ---
// --- Modified to display a scrollable dialog with better styling and smaller size ---
// --- Modified to be taller and manage cursor visibility ---
void TextEditor::showScrollableOutputDialog(const std::vector<std::string>& lines) {
    BuildOutputDialog::show(*m_renderer, lines);
}

CompilationResult TextEditor::runCompilationProcess() {
    CompilationResult result;
    result.success = false;

    if (currentBufferIdx() == -1) {
        msgwin("No file to compile.");
        return result;
    }

    EditorBuffer& buffer = currentBuffer();
    if (buffer.changed) {
        write_file(buffer);
    }
    m_pre_compile_view_state.line_num = buffer.current_line_num;
    m_pre_compile_view_state.col = buffer.cursor_col;
    m_pre_compile_view_state.first_visible_line_num = 0;

    result = m_buildSystem->runCompilationProcess(buffer);

    // Parse output for UI
    m_compile_output_lines.clear();

    // Use BuildSystem to parse for the UI error window
    std::string full_output;
    for(const auto& line : result.output_lines) full_output += line + "\n";
    
    std::vector<std::string> dummy;
    m_compile_output_lines = m_buildSystem->parseCompilerOutput(full_output, dummy);

    if (result.success) {
        m_compile_output_lines.push_back({"--- Compilation Successful ---", CompileMessage::CMSG_NONE});
    } else {
        m_compile_output_lines.push_back({"--- Compilation Failed ---", CompileMessage::CMSG_NONE});
    }

    m_compile_output_cursor_pos = 0;
    return result;
}

// --- Modified to show an immediate "Compiling..." message ---
void TextEditor::compileAndRun() {
    // 1. Immediately show a temporary "Compiling..." dialog
    int h = 5, w = 40;
    int starty = (m_renderer->getHeight() - h) / 2;
    int startx = (m_renderer->getWidth() - w) / 2;
    WINDOW* busy_win = newwin(h, w, starty, startx);
    wbkgd(busy_win, COLOR_PAIR(Renderer::CP_DIALOG));
    box(busy_win, 0, 0);
    mvwprintw(busy_win, 2, (w - 12) / 2, "Compiling...");
    wrefresh(busy_win);

    // 2. Run the compilation process in the background
    CompilationResult result = runCompilationProcess();

    // 3. Clean up the temporary dialog
    delwin(busy_win);
    touchwin(stdscr);
    refresh();

    // 4. Show the final scrollable results dialog
    showScrollableOutputDialog(result.output_lines);

    // 5. Proceed based on the result
    if (result.success) {
        def_prog_mode();
        endwin();

        std::string temp_output_file = "tedit_run_output.tmp";
        std::string run_cmd = "./" + result.executable_name + " > " + temp_output_file + " 2>&1";
        system(run_cmd.c_str());

        std::ifstream run_output_stream(temp_output_file);
        m_output_content = std::string((std::istreambuf_iterator<char>(run_output_stream)), std::istreambuf_iterator<char>());
        run_output_stream.close();
        remove(temp_output_file.c_str());
        m_output_content += "\n\n--- Press any key to return to the editor. ---";

        reset_prog_mode();
        refresh();
        m_output_screen_visible = true;
    } else {
        m_compile_output_visible = true;
        m_renderer->hideCursor();
    }
}

void TextEditor::compileOnly() {
    // 1. Immediately show a temporary "Compiling..." dialog
    int h = 5, w = 40;
    int starty = (m_renderer->getHeight() - h) / 2;
    int startx = (m_renderer->getWidth() - w) / 2;
    WINDOW* busy_win = newwin(h, w, starty, startx);
    wbkgd(busy_win, COLOR_PAIR(Renderer::CP_DIALOG));
    box(busy_win, 0, 0);
    mvwprintw(busy_win, 2, (w - 12) / 2, "Compiling...");
    wrefresh(busy_win);

    // 2. Run compilation and clean up the busy dialog
    CompilationResult result = runCompilationProcess();
    delwin(busy_win);
    touchwin(stdscr);
    refresh();

    // 3. Show final results
    showScrollableOutputDialog(result.output_lines);
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
            case CompileMessage::CMSG_ERROR: color = Renderer::CP_COMPILE_ERROR; break;
            case CompileMessage::CMSG_WARNING: color = Renderer::CP_COMPILE_WARNING; break;
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
    DialogResult res = ReplaceDialog::show(*m_renderer, m_search_term, m_replace_term);

    if (res.accepted()) {
        m_search_term  = res["find"];
        m_replace_term = res["replace"];

        if      (res["action"] == "replace")     PerformReplace();
        else if (res["action"] == "replace_all") PerformReplaceAll();
    }
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

    int replacements = SearchEngine::replaceAll(currentBuffer(), m_search_term, m_replace_term);

    if (replacements > 0) {
        currentBuffer().changed = true;
    }

    msgwin("Replaced " + std::to_string(replacements) + " occurrence(s).");
    update_cursor_and_scroll();
}

void TextEditor::handleToggleComment() {
    if (currentBufferIdx() == -1) return;
    EditorBuffer& buffer = currentBuffer();
    CreateUndoPoint(buffer);

    if (!buffer.selecting) {
        // --- SINGLE LINE COMMENT/UNCOMMENT ---
        std::string& line_text = buffer.current_line->text;
        size_t first_char_pos = line_text.find_first_not_of(" \t");

        if (first_char_pos != std::string::npos && line_text.substr(first_char_pos, 2) == "//") {
            // UNCOMMENT: Remove the // and an optional space after it
            line_text.erase(first_char_pos, 2);
            if (line_text.length() > first_char_pos && line_text[first_char_pos] == ' ') {
                line_text.erase(first_char_pos, 1);
            }
        } else {
            // COMMENT: Add // before the first non-whitespace character
            if (first_char_pos != std::string::npos) {
                line_text.insert(first_char_pos, "// ");
            } else {
                line_text.insert(0, "// ");
            }
        }
    } else {
        // --- MULTI-LINE (BLOCK) COMMENT/UNCOMMENT ---
        Line* p_start = buffer.selection_anchor_line;
        int p_start_linenum = buffer.selection_anchor_linenum;
        Line* p_end = buffer.current_line;
        int p_end_linenum = buffer.current_line_num;

        if (p_start_linenum > p_end_linenum) {
            std::swap(p_start, p_end);
        }

        // Check if ALL selected lines are already commented
        bool all_are_commented = true;
        Line* p_check = p_start;
        while (true) {
            size_t first_char_pos = p_check->text.find_first_not_of(" \t");
            // A line is considered "not commented" if it's not empty and doesn't start with //
            if (first_char_pos != std::string::npos && p_check->text.substr(first_char_pos, 2) != "//") {
                all_are_commented = false;
                break;
            }
            // Empty lines in the selection don't disqualify an uncomment operation
            if (p_check == p_end) break;
            p_check = p_check->next;
        }

        // Apply the operation to all lines in the selection range
        Line* p_apply = p_start;
        while (true) {
            if (all_are_commented) {
                // UNCOMMENT
                size_t first_char_pos = p_apply->text.find_first_not_of(" \t");
                if (first_char_pos != std::string::npos && p_apply->text.substr(first_char_pos, 2) == "//") {
                    p_apply->text.erase(first_char_pos, 2);
                    if (p_apply->text.length() > first_char_pos && p_apply->text[first_char_pos] == ' ') {
                        p_apply->text.erase(first_char_pos, 1);
                    }
                }
            } else {
                // COMMENT: Skip empty lines
                if (!p_apply->text.empty()) {
                    size_t first_char_pos = p_apply->text.find_first_not_of(" \t");
                    if (first_char_pos != std::string::npos) {
                        p_apply->text.insert(first_char_pos, "// ");
                    }
                }
            }

            if (p_apply == p_end) break;
            p_apply = p_apply->next;
        }
    }

    buffer.changed = true;
    update_cursor_and_scroll();
}

void TextEditor::msgwin(const std::string& s) {
    MessageDialog::show(*m_renderer, s);
}

void TextEditor::GoToLineDialog() {
    if (currentBufferIdx() == -1) return;
    int line = GoToLineDialog::show(*m_renderer, currentBuffer().current_line_num, currentBuffer().total_lines);
    if (line != -1) {
        currentBuffer().current_line_num = line;
        update_cursor_and_scroll();
    }
}

void TextEditor::CompileOptionsDialog() {
    if (currentBufferIdx() == -1) return;
    CompileOptionsDialog::show(*m_renderer, *m_buildSystem, currentBuffer());
}

void TextEditor::AboutBox() {
    MessageDialog::show(*m_renderer, "gedi C++ Editor\nVersion 1.0\nAn interactive IDE for C++ programmers.");
}

void TextEditor::loadHelpFile() {
    std::string helpPath = "help.hlp";
    if (!std::filesystem::exists(helpPath)) helpPath = "/usr/share/gedi/help.hlp";
    m_helpProvider->loadHelpFile(helpPath);
}

void TextEditor::showHelpDialog() {
    HelpDialog::show(*m_renderer, *m_helpProvider, m_help_history);
}
