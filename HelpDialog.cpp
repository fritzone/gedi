#include "HelpDialog.h"
#include <ncurses.h>
#include <algorithm>

void HelpDialog::show(Renderer& renderer, HelpProvider& helpProvider, std::vector<std::string>& help_history) {
    if (helpProvider.getHelpData().find("main") == helpProvider.getHelpData().end()) {
        return;
    }

    renderer.hideCursor();
    if(help_history.empty()){
        help_history.push_back("main");
    }

    int screen_h = renderer.getHeight();
    int screen_w = renderer.getWidth();

    // Capture the entire screen once at the beginning
    WINDOW* screen_backup = newwin(screen_h, screen_w, 0, 0);
    if (screen_backup) {
        copywin(stdscr, screen_backup, 0, 0, 0, 0, screen_h - 1, screen_w - 1, FALSE);
    }

    bool zoomed = false;
    int scroll_offset = 0;
    int selected_link_idx = 0;

    while(true) {
        // Restore entire screen from backup before drawing the dialog
        if (screen_backup) {
            copywin(screen_backup, stdscr, 0, 0, 0, 0, screen_h - 1, screen_w - 1, FALSE);
        }

        int h = zoomed ? screen_h - 1 : 25;
        int w = zoomed ? screen_w : 84;
        if (h > screen_h - 1) h = screen_h - 1;
        if (w > screen_w) w = screen_w;
        
        int starty = zoomed ? 1 : (screen_h - h) / 2;
        int startx = (screen_w - w) / 2;

        if (!zoomed) {
            renderer.drawShadow(startx, starty, w, h);
        }
        renderer.drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::DOUBLE, " Help System ", Renderer::CP_DIALOG_TITLE, A_BOLD);

        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        for (int i = 1; i < h - 1; ++i) mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        const std::string& current_id = help_history.back();
        const HelpSection& section = helpProvider.getHelpData().at(current_id);

        std::vector<HelpLine> render_lines;
        struct LinkInfo { const TextSegment* segment; int y_pos; };
        std::vector<LinkInfo> all_links;
        int content_width = w - 4;

        for (const auto& original_line : section.lines) {
            HelpLine current_render_line;
            int current_x = 0;

            if (original_line.segments.empty()) {
                render_lines.push_back(HelpLine());
                continue;
            }

            for (const auto& segment : original_line.segments) {
                std::string remaining_text = segment.text;
                while (!remaining_text.empty()) {
                    int space_left = content_width - current_x;
                    if (space_left <= 0) {
                        render_lines.push_back(current_render_line);
                        current_render_line.segments.clear();
                        current_x = 0;
                        space_left = content_width;
                    }

                    std::string part = remaining_text.substr(0, space_left);
                    current_render_line.segments.push_back({part, segment.style, segment.target_id});
                    current_x += part.length();
                    remaining_text = remaining_text.substr(part.length());
                }
            }
            render_lines.push_back(current_render_line);
        }

        for(size_t y = 0; y < render_lines.size(); ++y) {
            for (const auto& segment : render_lines[y].segments) {
                if (segment.style == STYLE_LINK) {
                    all_links.push_back({&segment, (int)y});
                }
            }
        }

        if (selected_link_idx >= (int)all_links.size()) {
            selected_link_idx = all_links.empty() ? -1 : 0;
        } else if (selected_link_idx < 0 && !all_links.empty()) {
            selected_link_idx = all_links.size() - 1;
        }

        int max_view_lines = h - 2;
        
        if (selected_link_idx != -1 && !all_links.empty()) {
            int link_y = all_links[selected_link_idx].y_pos;
            if (link_y < scroll_offset) scroll_offset = link_y;
            if (link_y >= scroll_offset + max_view_lines) scroll_offset = link_y - max_view_lines + 1;
        }
        
        if (scroll_offset + max_view_lines > (int)render_lines.size()) {
            scroll_offset = std::max(0, (int)render_lines.size() - max_view_lines);
        }

        for (int i = 0; i < max_view_lines; ++i) {
            int line_idx = scroll_offset + i;
            if (line_idx < (int)render_lines.size()) {
                int current_x = startx + 2;
                for (const auto& segment : render_lines[line_idx].segments) {
                    int color = Renderer::CP_DIALOG;
                    int style_flags = 0;

                    if (segment.style == STYLE_HEADER) {
                        color = Renderer::CP_DIALOG_TITLE;
                        style_flags = A_BOLD;
                    } else if (segment.style == STYLE_BOLD) {
                        style_flags = A_BOLD;
                    } else if (segment.style == STYLE_LINK) {
                        color = Renderer::CP_HIGHLIGHT;
                        style_flags = A_UNDERLINE;
                        
                        bool is_selected = false;
                        for (int li = 0; li < (int)all_links.size(); ++li) {
                            if (all_links[li].segment == &segment && li == selected_link_idx) {
                                is_selected = true;
                                break;
                            }
                        }
                        if (is_selected) {
                            color = Renderer::CP_MENU_SELECTED;
                            style_flags = A_BOLD;
                        }
                    }

                    attron(COLOR_PAIR(color) | style_flags);
                    mvaddstr(starty + 1 + i, current_x, segment.text.c_str());
                    attroff(COLOR_PAIR(color) | style_flags);
                    current_x += segment.text.length();
                }
            }
        }

        // Draw custom status line for help
        renderer.drawText(0, screen_h - 1, std::string(screen_w, ' '), Renderer::CP_STATUS_BAR);
        int pos = 1;
        auto drawKey = [&](const std::string& k, const std::string& desc) {
            renderer.drawText(pos, screen_h - 1, k, Renderer::CP_STATUS_BAR_HIGHLIGHT);
            pos += k.length() + 1;
            renderer.drawText(pos, screen_h - 1, desc, Renderer::CP_STATUS_BAR);
            pos += desc.length() + 2;
        };
        drawKey("Arrows/Tab", "Navigate");
        drawKey("Enter", "Select");
        drawKey("Backspace", "Back");
        drawKey("F5", zoomed ? "Restore" : "Zoom");
        drawKey("Esc", "Close");

        renderer.refresh();

        int ch = renderer.getChar();
        
        if (ch == 9 || ch == KEY_RIGHT || ch == KEY_DOWN) { 
            if (!all_links.empty()) {
                selected_link_idx = (selected_link_idx + 1) % all_links.size();
            } else if (ch == KEY_DOWN) {
                if (scroll_offset + max_view_lines < (int)render_lines.size()) scroll_offset++;
            }
        } else if (ch == KEY_BTAB || ch == KEY_LEFT || ch == KEY_UP) {
            if (!all_links.empty()) {
                selected_link_idx = (selected_link_idx - 1 + all_links.size()) % all_links.size();
            } else if (ch == KEY_UP) {
                if (scroll_offset > 0) scroll_offset--;
            }
        } else if (ch == KEY_F(5)) {
            zoomed = !zoomed;
            scroll_offset = 0; 
        } else if (ch == KEY_PPAGE) {
            scroll_offset -= max_view_lines;
            if (scroll_offset < 0) scroll_offset = 0;
        } else if (ch == KEY_NPAGE) {
            scroll_offset += max_view_lines;
        } else if (ch == KEY_ENTER || ch == 10 || ch == 13 || ch == ' ') {
            if (selected_link_idx != -1 && !all_links.empty()) {
                help_history.push_back(all_links[selected_link_idx].segment->target_id);
                scroll_offset = 0;
                selected_link_idx = 0;
            }
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8 || tolower(ch) == 'b') {
            if (help_history.size() > 1) {
                help_history.pop_back();
                scroll_offset = 0;
                selected_link_idx = 0;
            }
        } else if (ch == 27 || tolower(ch) == 'q' || ch == 'c') {
            break;
        }
    }

    // Final restoration and cleanup
    if (screen_backup) {
        copywin(screen_backup, stdscr, 0, 0, 0, 0, screen_h - 1, screen_w - 1, FALSE);
        delwin(screen_backup);
    }
    nodelay(stdscr, TRUE);
    renderer.showCursor();
}
