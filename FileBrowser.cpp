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

// --- Helper Functions for UI Rendering ---

static void drawDialogFrame(Renderer& renderer, int x, int y, int w, int h, const std::string& title) {
    renderer.drawShadow(x, y, w, h);
    renderer.drawBoxWithTitle(x, y, w, h, Renderer::CP_DIALOG, Renderer::DOUBLE, " " + title + " ", Renderer::CP_DIALOG_TITLE, A_BOLD);
    
    // Clear the interior
    wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
    for (int i = 1; i < h - 1; ++i) {
        mvwaddstr(stdscr, y + i, x + 1, std::string(w - 2, ' ').c_str());
    }
    wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
}

static void drawPathHeader(Renderer& renderer, int x, int y, int w, const std::string& path) {
    std::string path_str = " Path: " + path;
    if (path_str.length() > (size_t)w - 4) {
        path_str = " Path: ..." + path_str.substr(path_str.length() - (w - 10));
    }
    renderer.drawText(x + 1, y + 1, std::string(w - 2, ' '), Renderer::CP_HIGHLIGHT);
    renderer.drawText(x + 2, y + 1, path_str, Renderer::CP_HIGHLIGHT, A_BOLD);
}

static void drawFileList(Renderer& renderer, int x, int y, int w, int h, const std::vector<FileEntry>& entries, int selection, int top_of_list, bool has_focus) {
    int list_height = h;
    wattron(stdscr, COLOR_PAIR(Renderer::CP_LIST_BOX));
    for(int i=0; i<list_height; ++i) {
        mvwaddstr(stdscr, y + i, x, std::string(w, ' ').c_str());
    }
    wattroff(stdscr, COLOR_PAIR(Renderer::CP_LIST_BOX));

    for (int i = 0; i < list_height; ++i) {
        int entry_idx = top_of_list + i;
        if (entry_idx < (int)entries.size()) {
            const FileEntry& entry = entries[entry_idx];
            std::string prefix = entry.is_directory ? " [DIR] " : " [FIL] ";
            std::string display_name = prefix + entry.name;
            if (entry.is_directory && entry.name != "." && entry.name != "..") display_name += "/";
            
            if (display_name.length() > (size_t)w - 2) {
                display_name = display_name.substr(0, w - 5) + "...";
            } else {
                // Pad to full width for full-line selection
                display_name += std::string(w - display_name.length() - 1, ' ');
            }

            int color = (has_focus && entry_idx == selection) ? Renderer::CP_MENU_SELECTED : Renderer::CP_LIST_BOX;
            int style = (entry.is_directory && !(has_focus && entry_idx == selection)) ? A_BOLD : 0;
            renderer.drawText(x + 1, y + i, display_name, color, style);
        }
    }
}

static void drawFileDetails(Renderer& renderer, int x, int y, const FileEntry* selected) {
    if (!selected) return;

    int field_y = y;
    auto drawField = [&](const std::string& label, const std::string& value) {
        renderer.drawText(x, field_y, label, Renderer::CP_DIALOG);
        renderer.drawText(x + 2, field_y + 1, value, Renderer::CP_DIALOG, A_BOLD);
        field_y += 2; // More compact spacing
    };

    drawField("Type:", selected->is_directory ? "Dir" : "File");
    drawField("Owner:", selected->owner);
    drawField("Perms:", formatPermissions(selected->permissions));
    if (!selected->is_directory) {
        drawField("Size:", formatSize(selected->size));
    }
    drawField("Mod:", formatTime(selected->mod_time).substr(0, 10)); // Date only
}

static void drawInputArea(Renderer& renderer, int x, int y, int w, const std::string& label, const std::string& value, bool has_focus) {
    std::string full_label = " " + label + " ";
    int field_w = w - 2;
    std::string display_value = value;
    if (full_label.length() + display_value.length() > (size_t)field_w) {
        display_value = display_value.substr(display_value.length() - (field_w - full_label.length()));
    }
    
    // Draw full width background for the input field
    renderer.drawText(x + 1, y, std::string(field_w, ' '), Renderer::CP_LIST_BOX);
    renderer.drawText(x + 1, y, full_label + display_value, Renderer::CP_LIST_BOX);
    
    if (has_focus) {
        renderer.showCursor();
        // Use setCursor for precise positioning
        renderer.setCursor(x + 1 + full_label.length() + display_value.length(), y);
    }
}

static void drawActionButtons(Renderer& renderer, int x, int y, int w, const std::string& ok_text, bool ok_selected, const std::string& cancel_text, bool cancel_selected) {
    renderer.drawButton(x + w/2 - 12, y, ok_text, ok_selected);
    renderer.drawButton(x + w/2 + 2, y, cancel_text, cancel_selected);
}

// --- Main FileBrowser Methods ---

std::string FileBrowser::open(Renderer& renderer) {
    char CWD_BUFFER[1024];
    getcwd(CWD_BUFFER, sizeof(CWD_BUFFER));
    std::string current_path(CWD_BUFFER);

    int h = renderer.getHeight() - 10; // Slightly shorter
    int w = renderer.getWidth() - 20;  // Slightly narrower
    if (h < 15) h = 15;
    if (w < 60) w = 60;
    if (h > 20) h = 20; // Cap height
    if (w > 80) w = 80; // Cap width
    int starty = (renderer.getHeight() - h) / 2;
    int startx = (renderer.getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);
    nodelay(stdscr, FALSE);

    std::vector<FileEntry> entries;
    int selection = 0;
    int top_of_list = 0;
    std::string filename_buffer;
    int focus = 0; // 0: List, 1: Input, 2: Open, 3: Cancel

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

        drawDialogFrame(renderer, startx, starty, w, h-1, "Open File");
        drawPathHeader(renderer, startx, starty, w, current_path);

        int list_w = (w * 3) / 5;
        int visible_items = h - 10;
        int details_x = startx + list_w + 2;

        drawFileList(renderer, startx + 1, starty + 3, list_w, visible_items, entries, selection, top_of_list, focus == 0);
        
        if (selection < (int)entries.size()) {
            drawFileDetails(renderer, details_x, starty + 3, &entries[selection]);
        }

        drawActionButtons(renderer, startx, starty + h - 4, w, open_btn_text, focus == 2, cancel_btn_text, focus == 3);
        drawInputArea(renderer, startx + 1, starty + h - 6, w - 2, "File:", filename_buffer, focus == 1);

        if (focus != 1) renderer.hideCursor();
        renderer.refresh();

        wint_t ch = renderer.getChar();
        if (ch == 27) {
            timeout(50); wint_t next_ch = renderer.getChar(); timeout(-1);
            if (next_ch == ERR) { browser_active = false; continue; }
            switch(tolower(next_ch)) {
                case 'o': focus = 2; ch = KEY_ENTER; break;
                case 's': focus = 2; ch = KEY_ENTER; break;
                case 'c': focus = 3; ch = KEY_ENTER; break;
            }
        }

        switch (ch) {
            case 9: focus = (focus + 1) % 4; break;
            case KEY_BTAB: focus = (focus + 3) % 4; break;
            case KEY_LEFT: if (focus == 3) focus = 2; break;
            case KEY_RIGHT: if (focus == 1) focus = 0; else if (focus == 2) focus = 3; break;
            case KEY_UP:
                if (focus == 0 && selection > 0) {
                    selection--;
                    if (selection < top_of_list) top_of_list = selection;
                    if (!entries[selection].is_directory) filename_buffer = entries[selection].name;
                } else if (focus == 1) focus = 0;
                break;
            case KEY_DOWN:
                if (focus == 0) {
                    if (selection < (int)entries.size() - 1) {
                        selection++;
                        if (selection >= top_of_list + visible_items) top_of_list++;
                        if (!entries[selection].is_directory) filename_buffer = entries[selection].name;
                    } else focus = 1;
                }
                break;
            case KEY_BACKSPACE: case 127: case 8:
                if (focus == 1 && !filename_buffer.empty()) filename_buffer.pop_back();
                else if (focus == 0) { // Go to parent directory
                    if (chdir("..") == 0) {
                        getcwd(CWD_BUFFER, sizeof(CWD_BUFFER)); current_path = CWD_BUFFER;
                        selection = 0; top_of_list = 0; filename_buffer.clear();
                    }
                }
                break;
            case KEY_ENTER: case 10: case 13:
                if (focus == 3) browser_active = false;
                else if (focus == 2 || (focus == 1 && !filename_buffer.empty())) {
                    result_filename = current_path + "/" + filename_buffer;
                    browser_active = false;
                } else if (focus == 0 && selection < (int)entries.size()) {
                    FileEntry& sel = entries[selection];
                    if (sel.is_directory) {
                        if (chdir(sel.name.c_str()) == 0) {
                            getcwd(CWD_BUFFER, sizeof(CWD_BUFFER)); current_path = CWD_BUFFER;
                            selection = 0; top_of_list = 0; filename_buffer.clear();
                        }
                    } else {
                        filename_buffer = sel.name;
                        focus = 1;
                    }
                }
                break;
            default:
                if (focus == 1 && ch > 31 && ch < KEY_MIN) filename_buffer += wchar_to_utf8(ch);
                break;
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

    int h = renderer.getHeight() - 10;
    int w = renderer.getWidth() - 20;
    if (h < 15) h = 15;
    if (w < 60) w = 60;
    if (h > 20) h = 20;
    if (w > 80) w = 80;
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

        drawDialogFrame(renderer, startx, starty, w, h-1, "Save File As");
        drawPathHeader(renderer, startx, starty, w, current_path);

        int list_w = (w * 3) / 5;
        int visible_items = h - 10; // Even more space for gaps
        int details_x = startx + list_w + 2;

        drawFileList(renderer, startx + 1, starty + 3, list_w, visible_items, entries, selection, top_of_list, focus == 0);
        
        if (selection < (int)entries.size()) {
            drawFileDetails(renderer, details_x, starty + 3, &entries[selection]);
        }

        drawActionButtons(renderer, startx, starty + h - 4, w, save_btn_text, focus == 2, cancel_btn_text, focus == 3);
        drawInputArea(renderer, startx + 1, starty + h - 6, w - 2, "Save:", filename_buffer, focus == 1);

        if (focus != 1) renderer.hideCursor();
        renderer.refresh();

        wint_t ch = renderer.getChar();
        if (ch == 27) {
            timeout(50); wint_t next_ch = renderer.getChar(); timeout(-1);
            if (next_ch == ERR) { browser_active = false; continue; }
            switch(tolower(next_ch)) {
                case 's': focus = 2; ch = KEY_ENTER; break;
                case 'c': focus = 3; ch = KEY_ENTER; break;
            }
        }

        switch (ch) {
            case 9: focus = (focus + 1) % 4; break;
            case KEY_BTAB: focus = (focus + 3) % 4; break;
            case KEY_LEFT: if (focus == 3) focus = 2; break;
            case KEY_RIGHT: if (focus == 1) focus = 0; else if (focus == 2) focus = 3; break;
            case KEY_UP:
                if (focus == 0 && selection > 0) {
                    selection--;
                    if (selection < top_of_list) top_of_list = selection;
                    if (!entries[selection].is_directory) filename_buffer = entries[selection].name;
                } else if (focus == 1) focus = 0;
                break;
            case KEY_DOWN:
                if (focus == 0) {
                    if (selection < (int)entries.size() - 1) {
                        selection++;
                        if (selection >= top_of_list + visible_items) top_of_list++;
                        if (!entries[selection].is_directory) filename_buffer = entries[selection].name;
                    } else focus = 1;
                }
                break;
            case KEY_BACKSPACE: case 127: case 8:
                if (focus == 1 && !filename_buffer.empty()) filename_buffer.pop_back();
                else if (focus == 0) {
                    if (chdir("..") == 0) {
                        getcwd(CWD_BUFFER, sizeof(CWD_BUFFER)); current_path = CWD_BUFFER;
                        selection = 0; top_of_list = 0; filename_buffer.clear();
                    }
                }
                break;
            case KEY_ENTER: case 10: case 13:
                if (focus == 3) browser_active = false;
                else if (focus == 2 || (focus == 1 && !filename_buffer.empty())) {
                    result_filename = current_path + "/" + filename_buffer;
                    browser_active = false;
                } else if (focus == 0 && selection < (int)entries.size()) {
                    FileEntry& sel = entries[selection];
                    if (sel.is_directory) {
                        if (chdir(sel.name.c_str()) == 0) {
                            getcwd(CWD_BUFFER, sizeof(CWD_BUFFER)); current_path = CWD_BUFFER;
                            selection = 0; top_of_list = 0; filename_buffer.clear();
                        }
                    } else {
                        filename_buffer = sel.name;
                        focus = 1;
                    }
                }
                break;
            default:
                if (focus == 1 && ch > 31 && ch < KEY_MIN) filename_buffer += wchar_to_utf8(ch);
                break;
        }
    }

    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE); delwin(behind);
    nodelay(stdscr, TRUE); renderer.showCursor();
    return result_filename;
}
