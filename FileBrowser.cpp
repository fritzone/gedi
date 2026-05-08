#include "FileBrowser.h"
#include "Renderer.h"
#include "utils.h"

#include <ncurses.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <algorithm>
#include <vector>
#include <string>

std::string FileBrowser::open(Renderer& renderer) {
    char CWD_BUFFER[1024];
    getcwd(CWD_BUFFER, sizeof(CWD_BUFFER));
    std::string current_path(CWD_BUFFER);

    int h = renderer.getHeight() - 8;
    int w = renderer.getWidth() - 12;
    if (h < 14) h = 14;
    if (w < 60) w = 60;
    int starty = (renderer.getHeight() - h) / 2;
    int startx = (renderer.getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

    nodelay(stdscr, FALSE);

    std::vector<FileEntry> entries;
    int selection = 0;
    int top_of_list = 0;
    std::string search_string;
    int focus = 0; // 0: List, 1: Open, 2: Cancel

    std::string open_btn_text = " &Open ";
    std::string cancel_btn_text = " &Cancel ";

    std::string result_filename = "";

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

        renderer.drawShadow(startx, starty, w, h);
        renderer.drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::SINGLE, " Open File ", Renderer::CP_DIALOG_TITLE, A_BOLD);

        bool needs_redraw = true;
        while(true) {
            if (needs_redraw) {
                wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
                for (int i = 1; i < h - 1; ++i) mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
                wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

                std::string path_str = " " + current_path;
                if (path_str.length() > (size_t)w - 2) path_str = "..." + path_str.substr(path_str.length() - (w - 5));
                renderer.drawText(startx + 1, starty + 1, std::string(w - 2, ' '), Renderer::CP_HIGHLIGHT);
                renderer.drawText(startx + 2, starty + 1, path_str, Renderer::CP_HIGHLIGHT);

                int list_w = (w * 2) / 3;
                int details_x = startx + list_w + 1;
                wattron(stdscr, COLOR_PAIR(Renderer::CP_DEFAULT_TEXT));
                mvvline(starty + 2, details_x - 1, ACS_VLINE, h-5);
                mvhline(starty + h - 3, startx + 1, ACS_HLINE, w-2);
                wattroff(stdscr, COLOR_PAIR(Renderer::CP_DEFAULT_TEXT));

                int list_height = h - 6;
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

                        int color = (focus == 0 && entry_idx == selection) ? Renderer::CP_MENU_SELECTED : Renderer::CP_LIST_BOX;
                        int style = (entry.is_directory && !(focus == 0 && entry_idx == selection)) ? A_BOLD : 0;
                        renderer.drawText(startx + 2, starty + 2 + i, display_name, color, style);
                    }
                }

                if (selection < (int)entries.size()) {
                    FileEntry& selected = entries[selection];
                    int field_y = starty + 3;
                    renderer.drawText(details_x + 2, field_y, "Type:", Renderer::CP_DIALOG);
                    renderer.drawText(details_x + 4, field_y + 1, selected.is_directory ? "Directory" : "File", Renderer::CP_DIALOG, A_BOLD);
                    field_y += 3;
                    renderer.drawText(details_x + 2, field_y, "Owner:", Renderer::CP_DIALOG);
                    renderer.drawText(details_x + 4, field_y + 1, selected.owner + ":" + selected.group, Renderer::CP_DIALOG, A_BOLD);
                    field_y += 3;
                    renderer.drawText(details_x + 2, field_y, "Perms:", Renderer::CP_DIALOG);
                    renderer.drawText(details_x + 4, field_y + 1, formatPermissions(selected.permissions), Renderer::CP_DIALOG, A_BOLD);
                    field_y += 3;
                    if (!selected.is_directory) {
                        renderer.drawText(details_x + 2, field_y, "Size:", Renderer::CP_DIALOG);
                        renderer.drawText(details_x + 4, field_y + 1, formatSize(selected.size), Renderer::CP_DIALOG, A_BOLD);
                        field_y += 3;
                    }
                    renderer.drawText(details_x + 2, field_y, "Modified:", Renderer::CP_DIALOG);
                    renderer.drawText(details_x + 4, field_y + 1, formatTime(selected.mod_time), Renderer::CP_DIALOG, A_BOLD);
                }

                std::string search_prompt = "Find: " + search_string;
                renderer.drawText(startx + 1, starty + h - 4, std::string(w - 2, ' '), Renderer::CP_LIST_BOX);
                renderer.drawText(startx + 2, starty + h - 4, search_prompt, Renderer::CP_LIST_BOX);

                renderer.drawButton(startx + w/2 - 15, starty + h - 2, open_btn_text, focus == 1);
                renderer.drawButton(startx + w/2 + 5, starty + h - 2, cancel_btn_text, focus == 2);

                if (focus == 0) { renderer.showCursor(); move(starty + h - 4, startx + 2 + search_prompt.length()); }
                else { renderer.hideCursor(); }

                renderer.refresh();
                needs_redraw = false;
            }

            wint_t ch = renderer.getChar();
            bool break_inner = false;

            if (ch == 27) {
                timeout(50);
                wint_t next_ch = renderer.getChar();
                timeout(-1);
                if (next_ch == ERR) { browser_active = false; break; }
                switch(tolower(next_ch)) {
                case 'o': focus=1; ch = KEY_ENTER; break;
                case 'c': focus=2; ch = KEY_ENTER; break;
                }
            }

            switch (ch) {
            case 9: focus = (focus + 1) % 3; needs_redraw = true; break;
            case KEY_LEFT: if (focus == 2) focus = 1; needs_redraw = true; break;
            case KEY_RIGHT: if (focus == 1) focus = 2; needs_redraw = true; break;
            case KEY_UP: if (focus == 0 && selection > 0) { selection--; needs_redraw = true; } if (selection < top_of_list) top_of_list = selection; search_string.clear(); break;
            case KEY_DOWN: if (focus == 0 && selection < (int)entries.size() - 1) { selection++; needs_redraw = true; } if (selection >= top_of_list + h - 6) top_of_list++; search_string.clear(); break;
            case KEY_ENTER: case 10: case 13:
                if (focus == 2) {
                    browser_active = false;
                    break_inner = true;
                } else if (focus == 1 || focus == 0) {
                    if (selection < (int)entries.size()) {
                        FileEntry& selected_entry = entries[selection];
                        if (selected_entry.is_directory) {
                            if (chdir((current_path + "/" + selected_entry.name).c_str()) == 0) {
                                getcwd(CWD_BUFFER, sizeof(CWD_BUFFER)); current_path = CWD_BUFFER;
                                selection = 0; top_of_list = 0; search_string.clear();
                                break_inner = true;
                            }
                        } else {
                            result_filename = current_path + "/" + selected_entry.name;
                            browser_active = false; break_inner = true;
                        }
                    }
                }
                break;
            case KEY_BACKSPACE: case 127: case 8:
                if (focus == 0 && !search_string.empty()) {
                    search_string.pop_back();
                    needs_redraw = true;
                }
                break;
            default:
                if (focus == 0 && ch > 31 && ch < KEY_MIN) {
                    search_string += tolower(ch);
                    for (size_t i = 0; i < entries.size(); ++i) {
                        std::string lower_name = entries[i].name;
                        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                        if (lower_name.rfind(search_string, 0) == 0) {
                            selection = i;
                            if (selection < top_of_list || selection >= top_of_list + h - 6) {
                                top_of_list = selection;
                            }
                            break;
                        }
                    }
                    needs_redraw = true;
                }
                break;
            }
            if (break_inner) break;
        }
    }

    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE); delwin(behind);
    nodelay(stdscr, TRUE); renderer.showCursor();
    return result_filename;
}

std::string FileBrowser::save(Renderer& renderer, const std::string& current_filename) {
    char CWD_BUFFER[1024];
    getcwd(CWD_BUFFER, sizeof(CWD_BUFFER));
    std::string current_path(CWD_BUFFER);

    size_t last_slash = current_filename.find_last_of('/');
    std::string filename_buffer = (last_slash != std::string::npos) ? current_filename.substr(last_slash + 1) : current_filename;

    int h = renderer.getHeight() - 8;
    int w = renderer.getWidth() - 12;
    if (h < 16) h = 16;
    if (w < 60) w = 60;
    int starty = (renderer.getHeight() - h) / 2;
    int startx = (renderer.getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);
    nodelay(stdscr, FALSE);

    std::vector<FileEntry> entries;
    int selection = 0;
    int top_of_list = 0;
    int focus = 1; // 0: List, 1: Input, 2: Save, 3: Cancel

    std::string save_btn_text = " &Save ";
    std::string cancel_btn_text = " &Cancel ";

    std::string result_filename = "";

    bool browser_active = true;
    while (browser_active) {
        entries.clear();
        // ... (skipping some code for match)
        std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b){
            if (a.name == ".") return true; if (b.name == ".") return false;
            if (a.name == "..") return true; if (b.name == "..") return false;
            if (a.is_directory != b.is_directory) return a.is_directory;
            return a.name < b.name;
        });

        renderer.drawShadow(startx, starty, w, h);
        renderer.drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::SINGLE, " Save File As ", Renderer::CP_DIALOG_TITLE, A_BOLD);

        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        for (int i = 1; i < h - 1; ++i) mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        std::string path_str = " " + current_path;
        if (path_str.length() > (size_t)w - 2) path_str = "..." + path_str.substr(path_str.length() - (w - 5));
        renderer.drawText(startx + 1, starty + 1, std::string(w - 2, ' '), Renderer::CP_HIGHLIGHT);
        renderer.drawText(startx + 2, starty + 1, path_str, Renderer::CP_HIGHLIGHT);

        int list_w = (w * 2) / 3;
        int details_x = startx + list_w + 1;
        wattron(stdscr, COLOR_PAIR(Renderer::CP_DEFAULT_TEXT));
        mvvline(starty + 2, details_x - 1, ACS_VLINE, h - 7);
        mvhline(starty + h - 5, startx + 1, ACS_HLINE, w-2);
        mvhline(starty + h - 3, startx + 1, ACS_HLINE, w-2);
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DEFAULT_TEXT));

        int list_height = h - 7;
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

                int color = (focus == 0 && entry_idx == selection) ? Renderer::CP_MENU_SELECTED : Renderer::CP_LIST_BOX;
                int style = (entry.is_directory && !(focus == 0 && entry_idx == selection)) ? A_BOLD : 0;
                renderer.drawText(startx + 2, starty + 2 + i, display_name, color, style);
            }
        }

        if (selection < (int)entries.size()) {
            FileEntry& selected = entries[selection];
            int field_y = starty + 3;
            renderer.drawText(details_x + 2, field_y, "Type:", Renderer::CP_DIALOG);
            renderer.drawText(details_x + 4, field_y + 1, selected.is_directory ? "Directory" : "File", Renderer::CP_DIALOG, A_BOLD);
            field_y += 3;
            renderer.drawText(details_x + 2, field_y, "Perms:", Renderer::CP_DIALOG);
            renderer.drawText(details_x + 4, field_y + 1, formatPermissions(selected.permissions), Renderer::CP_DIALOG, A_BOLD);
            field_y += 3;
            if (!selected.is_directory) {
                renderer.drawText(details_x + 2, field_y, "Size:", Renderer::CP_DIALOG);
                renderer.drawText(details_x + 4, field_y + 1, formatSize(selected.size), Renderer::CP_DIALOG, A_BOLD);
            }
        }

        std::string input_prompt = "Save Name: ";
        renderer.drawText(startx + 1, starty + h - 4, std::string(w - 2, ' '), Renderer::CP_LIST_BOX);
        renderer.drawText(startx + 2, starty + h - 4, input_prompt + filename_buffer, Renderer::CP_LIST_BOX);

        renderer.drawButton(startx + w/2 - 15, starty + h - 2, save_btn_text, focus == 2);
        renderer.drawButton(startx + w/2 + 5, starty + h - 2, cancel_btn_text, focus == 3);

        if (focus == 1) { renderer.showCursor(); move(starty + h - 4, startx + 2 + input_prompt.length() + filename_buffer.length()); }
        else { renderer.hideCursor(); }

        renderer.refresh();

        wint_t ch = renderer.getChar();

        if (ch == 27) {
            timeout(50);
            wint_t next_ch = renderer.getChar();
            timeout(-1);
            if (next_ch == ERR) { browser_active = false; continue; }
            switch(tolower(next_ch)) {
            case 's': focus=2; ch = KEY_ENTER; break;
            case 'c': focus=3; ch = KEY_ENTER; break;
            }
        }

        switch (ch) {
        case 9: focus = (focus + 1) % 4; break;
        case KEY_LEFT: if (focus == 3) focus = 2; break;
        case KEY_RIGHT: if (focus == 2) focus = 3; break;
        case KEY_UP:
            if (focus == 1) focus = 0;
            else if (focus == 0 && selection > 0) {
                selection--;
                if (!entries[selection].is_directory) filename_buffer = entries[selection].name;
                else filename_buffer.clear();
            }
            if (selection < top_of_list) top_of_list = selection;
            break;
        case KEY_DOWN:
            if (focus == 0) {
                if (selection < (int)entries.size() - 1) {
                    selection++;
                    if (!entries[selection].is_directory) filename_buffer = entries[selection].name;
                    else filename_buffer.clear();
                    if (selection >= top_of_list + h - 7) top_of_list++;
                } else { focus = 1; }
            }
            break;
        case KEY_BACKSPACE: case 127: case 8:
            if (focus == 1 && !filename_buffer.empty()) { filename_buffer.pop_back(); }
            break;
        case KEY_ENTER: case 10: case 13:
            if (focus == 3) {
                browser_active = false;
            } else if (focus == 2 || (focus == 1 && !filename_buffer.empty())) {
                result_filename = current_path + "/" + filename_buffer;
                browser_active = false;
            } else if (focus == 0 && selection < (int)entries.size()) {
                FileEntry& selected_entry = entries[selection];
                if (selected_entry.is_directory) {
                    if (chdir((current_path + "/" + selected_entry.name).c_str()) == 0) {
                        getcwd(CWD_BUFFER, sizeof(CWD_BUFFER)); current_path = CWD_BUFFER;
                        selection = 0; top_of_list = 0; filename_buffer.clear();
                    }
                } else {
                    filename_buffer = selected_entry.name;
                    focus = 1;
                }
            }
            break;
        default:
            if (focus == 1 && ch > 31 && ch < KEY_MIN) {
                filename_buffer += wchar_to_utf8(ch);
            }
            break;
        }
    }

    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE); delwin(behind);
    nodelay(stdscr, TRUE); renderer.showCursor();
    return result_filename;
}
