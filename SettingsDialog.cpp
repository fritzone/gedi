#include "SettingsDialog.h"
#include <ncurses.h>
#include <algorithm>

void SettingsDialog::show(Renderer& renderer, Config& config, ConfigManager& configManager, const std::vector<std::string>& themes) {
    int h = 25, w = 60;
    int starty = (renderer.getHeight() - h) / 2;
    int startx = (renderer.getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

    bool temp_smart_indent = config.smart_indentation;
    int temp_indent_width = config.indentation_width;
    bool temp_show_line_numbers = config.show_line_numbers;
    int temp_theme_idx = 0;
    for(size_t i = 0; i < themes.size(); ++i) {
        if (themes[i] == config.color_scheme_name) { temp_theme_idx = i; break; }
    }

    int focus_group = 0; // 0: Indent, 1: View, 2: Theme, 3: Buttons
    int focus_item[4] = {0, 0, temp_theme_idx, 0};

    std::string save_btn_text = " &Save ";
    std::string cancel_btn_text = " &Cancel ";

    renderer.drawShadow(startx, starty, w, h);
    renderer.drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::DOUBLE, " Editor Settings ", Renderer::CP_DIALOG_TITLE, A_BOLD);

    bool dialog_active = true;
    bool pressed = false;
    while(dialog_active) {
        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        for (int i = 1; i < h - 1; ++i) mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        // Group 1: Indentation
        renderer.drawBoxWithTitle(startx + 2, starty + 2, w - 4, 4, Renderer::CP_DIALOG, Renderer::SINGLE, " &Indentation ", Renderer::CP_DIALOG, (focus_group == 0 ? A_BOLD : 0));
        renderer.drawText(startx + 4, starty + 3, (temp_smart_indent ? "[X]" : "[ ]"), (focus_group == 0 && focus_item[0] == 0) ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
        renderer.drawText(startx + 8, starty + 3, "Smart Indent", Renderer::CP_DIALOG);
        renderer.drawText(startx + 4, starty + 4, "< " + std::to_string(temp_indent_width) + " >", (focus_group == 0 && focus_item[0] == 1) ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
        renderer.drawText(startx + 12, starty + 4, "Tab Size", Renderer::CP_DIALOG);

        // Group 2: View
        renderer.drawBoxWithTitle(startx + 2, starty + 7, w - 4, 3, Renderer::CP_DIALOG, Renderer::SINGLE, " &View ", Renderer::CP_DIALOG, (focus_group == 1 ? A_BOLD : 0));
        renderer.drawText(startx + 4, starty + 8, (temp_show_line_numbers ? "[X]" : "[ ]"), (focus_group == 1 && focus_item[1] == 0) ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
        renderer.drawText(startx + 8, starty + 8, "Show Line Numbers", Renderer::CP_DIALOG);

        // Group 3: Color Scheme
        int color_box_h = h - 15;
        renderer.drawBoxWithTitle(startx + 2, starty + 11, w - 4, color_box_h, Renderer::CP_DIALOG, Renderer::SINGLE, " Col&or Scheme ", Renderer::CP_DIALOG, (focus_group == 2 ? A_BOLD : 0));
        int list_height = color_box_h - 2;
        int top_of_list = 0;
        if (temp_theme_idx >= list_height) {
            top_of_list = temp_theme_idx - list_height + 1;
        }
        for(int i = 0; i < list_height; ++i) {
            int current_theme_idx = top_of_list + i;
            if (current_theme_idx < (int)themes.size()) {
                renderer.drawText(startx + 4, starty + 12 + i, (current_theme_idx == temp_theme_idx ? "(•)" : "( )"), (focus_group == 2 && current_theme_idx == focus_item[2]) ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
                renderer.drawText(startx + 8, starty + 12 + i, themes[current_theme_idx], Renderer::CP_DIALOG);
            }
        }

        renderer.drawButton(startx + w / 2 - 15, starty + h - 3, save_btn_text, focus_group == 3 && focus_item[3] == 0, pressed && focus_group == 3 && focus_item[3] == 0);
        renderer.drawButton(startx + w / 2 + 5, starty + h - 3, cancel_btn_text, focus_group == 3 && focus_item[3] == 1, pressed && focus_group == 3 && focus_item[3] == 1);

        renderer.hideCursor();
        renderer.refresh();

        if (pressed) {
            napms(100);
            break;
        }

        wint_t ch = renderer.getChar();
        if (ch == 27) {
            timeout(1);
            wint_t next_ch = renderer.getChar();
            timeout(-1);
            if (next_ch == ERR) { dialog_active = false; break; }
            switch (tolower(next_ch)) {
                case 'i': focus_group = 0; focus_item[0] = 0; break;
                case 'v': focus_group = 1; focus_item[1] = 0; break;
                case 'o': focus_group = 2; focus_item[2] = temp_theme_idx; break;
                case 's': focus_group = 3; focus_item[3] = 0; pressed = true; break;
                case 'c': focus_group = 3; focus_item[3] = 1; pressed = true; break;
            }
        } else {
            switch (ch) {
                case 9: focus_group = (focus_group + 1) % 4; break;
                case KEY_BTAB: focus_group = (focus_group + 3) % 4; break;
                case KEY_UP:
                    if (focus_group == 0 && focus_item[0] > 0) focus_item[0]--;
                    else if (focus_group == 2 && focus_item[2] > 0) focus_item[2]--;
                    break;
                case KEY_DOWN:
                    if (focus_group == 0 && focus_item[0] < 1) focus_item[0]++;
                    else if (focus_group == 2 && focus_item[2] < (int)themes.size() - 1) focus_item[2]++;
                    break;
                case KEY_LEFT:
                    if (focus_group == 0 && focus_item[0] == 1 && temp_indent_width > 1) temp_indent_width--;
                    else if (focus_group == 3 && focus_item[3] > 0) focus_item[3]--;
                    break;
                case KEY_RIGHT:
                    if (focus_group == 0 && focus_item[0] == 1 && temp_indent_width < 16) temp_indent_width++;
                    else if (focus_group == 3 && focus_item[3] < 1) focus_item[3]++;
                    break;
                case ' ': case KEY_ENTER: case 10: case 13:
                    if (focus_group == 0) {
                        if (focus_item[0] == 0) temp_smart_indent = !temp_smart_indent;
                    } else if (focus_group == 1) {
                        temp_show_line_numbers = !temp_show_line_numbers;
                    } else if (focus_group == 2) {
                        temp_theme_idx = focus_item[2];
                    } else if (focus_group == 3) {
                        if (focus_item[3] == 0) { // Save
                            config.smart_indentation = temp_smart_indent;
                            config.indentation_width = temp_indent_width;
                            config.show_line_numbers = temp_show_line_numbers;
                            config.color_scheme_name = themes[temp_theme_idx];
                            configManager.saveConfig(config);
                            renderer.loadColors(configManager.loadThemes()[config.color_scheme_name]);
                        }
                        pressed = true;
                    }
                    break;
            }
        }
    }

    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE);
    delwin(behind);
    nodelay(stdscr, TRUE);
    renderer.showCursor();
}
