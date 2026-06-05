#include "HelpDialog.h"
#include "curses_compat.h"
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
        struct LinkInfo { const TextSegment* segment; int y_pos; int x_start; int len; };
        std::vector<LinkInfo> all_links;
        int content_width = w - 5;  // reserve one column on the right for the scrollbar

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
                        if (!remaining_text.empty() && remaining_text[0] == ' ')
                            remaining_text = remaining_text.substr(1);
                        continue;
                    }

                    if ((int)remaining_text.length() <= space_left) {
                        current_render_line.segments.push_back({remaining_text, segment.style, segment.target_id});
                        current_x += (int)remaining_text.length();
                        break;
                    }

                    // Text overflows — find last space within the available width
                    size_t break_pos = remaining_text.rfind(' ', space_left - 1);

                    if (break_pos != std::string::npos && break_pos > 0) {
                        std::string part = remaining_text.substr(0, break_pos);
                        current_render_line.segments.push_back({part, segment.style, segment.target_id});
                        render_lines.push_back(current_render_line);
                        current_render_line.segments.clear();
                        current_x = 0;
                        remaining_text = remaining_text.substr(break_pos + 1);
                    } else if (current_x > 0) {
                        // No word boundary but not at line start — wrap and retry
                        render_lines.push_back(current_render_line);
                        current_render_line.segments.clear();
                        current_x = 0;
                        if (!remaining_text.empty() && remaining_text[0] == ' ')
                            remaining_text = remaining_text.substr(1);
                    } else {
                        // At line start with no space — hard-break the long token
                        std::string part = remaining_text.substr(0, space_left);
                        current_render_line.segments.push_back({part, segment.style, segment.target_id});
                        current_x += (int)part.length();
                        remaining_text = remaining_text.substr(space_left);
                        render_lines.push_back(current_render_line);
                        current_render_line.segments.clear();
                        current_x = 0;
                    }
                }
            }
            render_lines.push_back(current_render_line);
        }

        for (size_t y = 0; y < render_lines.size(); ++y) {
            int x = 0;
            for (const auto& segment : render_lines[y].segments) {
                if (segment.style == STYLE_LINK)
                    all_links.push_back({&segment, (int)y, x, (int)segment.text.size()});
                x += (int)segment.text.size();
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

        // Vertical scrollbar
        bool sb_visible  = (int)render_lines.size() > max_view_lines;
        int  sb_bar_x    = startx + w - 2;
        int  sb_arrow_top = starty + 1;
        int  sb_arrow_bot = starty + h - 2;
        int  sb_track_top = sb_arrow_top + 1;
        int  sb_track_h   = max_view_lines - 2;

        if (sb_visible) {
            attron(COLOR_PAIR(Renderer::CP_HIGHLIGHT));
            mvaddch(sb_arrow_top, sb_bar_x, ACS_UARROW);
            mvaddch(sb_arrow_bot, sb_bar_x, ACS_DARROW);
            for (int i = 0; i < sb_track_h; ++i)
                mvaddch(sb_track_top + i, sb_bar_x, ACS_CKBOARD);
            if (sb_track_h > 0) {
                float proportion = (float)scroll_offset / ((int)render_lines.size() - max_view_lines);
                if (proportion > 1.0f) proportion = 1.0f;
                int thumb_y = sb_track_top + (int)((sb_track_h - 1) * proportion);
                mvaddch(thumb_y, sb_bar_x, ACS_BLOCK);
            }
            attroff(COLOR_PAIR(Renderer::CP_HIGHLIGHT));
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

        if (ch == KEY_RESIZE) {
            // Backing screen was blanked and resized. Refresh the cached screen
            // dimensions and re-snapshot the (now blank) backdrop so the next
            // iteration re-lays-out and repaints the help window at the new size.
            renderer.updateDimensions();
            screen_h = renderer.getHeight();
            screen_w = renderer.getWidth();
            renderer.repaintBackground();   // redraw the editor behind at the new size
            if (screen_backup) delwin(screen_backup);
            screen_backup = newwin(screen_h, screen_w, 0, 0);
            if (screen_backup)
                copywin(stdscr, screen_backup, 0, 0, 0, 0, screen_h - 1, screen_w - 1, FALSE);
            continue;
        }

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
        } else if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                bool is_pos_rpt  = (ev.bstate & REPORT_MOUSE_POSITION) != 0;
                bool is_press    = (ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) != 0 && !is_pos_rpt;
                bool scroll_up   = (ev.bstate & BUTTON4_PRESSED) != 0;
                bool scroll_dn   = (ev.bstate & BUTTON5_PRESSED) != 0;
                int  mx = ev.x, my = ev.y;

                // Scroll wheel — 3 lines per tick
                if (scroll_up || scroll_dn) {
                    for (int i = 0; i < 3; ++i) {
                        if (scroll_up && scroll_offset > 0) --scroll_offset;
                        if (scroll_dn && scroll_offset + max_view_lines < (int)render_lines.size()) ++scroll_offset;
                    }
                }

                if (is_press) {
                    // Click outside the dialog → close
                    if (mx < startx || mx >= startx + w || my < starty || my >= starty + h) {
                        break;
                    }

                    // Click on scrollbar arrows / track
                    if (sb_visible && mx == sb_bar_x) {
                        if (my == sb_arrow_top) {
                            if (scroll_offset > 0) --scroll_offset;
                        } else if (my == sb_arrow_bot) {
                            if (scroll_offset + max_view_lines < (int)render_lines.size()) ++scroll_offset;
                        } else if (my > sb_arrow_top && my < sb_arrow_bot && sb_track_h > 1) {
                            float proportion = (float)(my - sb_track_top) / (sb_track_h - 1);
                            scroll_offset = (int)(proportion * ((int)render_lines.size() - max_view_lines));
                            if (scroll_offset < 0) scroll_offset = 0;
                        }
                    }

                    // Click on a link → follow it
                    for (int li = 0; li < (int)all_links.size(); ++li) {
                        const auto& lnk = all_links[li];
                        if (lnk.y_pos < scroll_offset || lnk.y_pos >= scroll_offset + max_view_lines)
                            continue;
                        int sy = starty + 1 + (lnk.y_pos - scroll_offset);
                        int sx = startx + 2 + lnk.x_start;
                        if (my == sy && mx >= sx && mx < sx + lnk.len) {
                            help_history.push_back(lnk.segment->target_id);
                            scroll_offset     = 0;
                            selected_link_idx = 0;
                            break;
                        }
                    }
                }
            }
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
