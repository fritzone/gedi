#include "FileBrowser.h"
#include "utils.h"

#include <ncurses.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <algorithm>
#include <string>

// ═══════════════════════════════════════════════════════════════════════════════
// FileBrowser.cpp
//
// All three public methods (open / save / selectDirectory) delegate to run().
// run() is parameterised by Mode, which controls:
//   - The dialog title and button labels
//   - Whether the filename input row is shown (hidden for SELECT_DIR)
//   - What counts as a valid selection on Enter/Ok
//   - Which entries are highlighted as selectable
// ═══════════════════════════════════════════════════════════════════════════════

// ── Public entry points ───────────────────────────────────────────────────────

std::string FileBrowser::open(Renderer& renderer)
{
    return run(renderer, Mode::OPEN, "");
}

std::string FileBrowser::save(Renderer& renderer, const std::string& current_filename)
{
    // Pre-fill the filename field with the basename of the current file
    std::string initial;
    auto slash = current_filename.find_last_of('/');
    initial = (slash != std::string::npos)
                  ? current_filename.substr(slash + 1)
                  : current_filename;
    return run(renderer, Mode::SAVE, initial);
}

std::string FileBrowser::selectDirectory(Renderer& renderer)
{
    return run(renderer, Mode::SELECT_DIR, "");
}

// ── Directory helpers ─────────────────────────────────────────────────────────

std::vector<FileEntry> FileBrowser::readDirectory(const std::string& path)
{
    std::vector<FileEntry> entries;
    DIR* dir = opendir(path.c_str());
    if (!dir) return entries;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name      = ent->d_name;
        std::string full_path = path + "/" + name;
        struct stat st{};
        if (stat(full_path.c_str(), &st) != 0) continue;

        struct passwd* pw = getpwuid(st.st_uid);
        struct group*  gr = getgrgid(st.st_gid);
        entries.push_back({
            .name        = name,
            .is_directory= S_ISDIR(st.st_mode),
            .size        = st.st_size,
            .mod_time    = st.st_mtime,
            .permissions = st.st_mode,
            .owner       = pw ? pw->pw_name : std::to_string(st.st_uid),
            .group       = gr ? gr->gr_name : std::to_string(st.st_gid),
        });
    }
    closedir(dir);
    return entries;
}

void FileBrowser::sortEntries(std::vector<FileEntry>& entries)
{
    std::sort(entries.begin(), entries.end(),
              [](const FileEntry& a, const FileEntry& b) {
                  if (a.name == ".")  return true;
                  if (b.name == ".")  return false;
                  if (a.name == "..") return true;
                  if (b.name == "..") return false;
                  if (a.is_directory != b.is_directory) return a.is_directory;
                  return a.name < b.name;
              });
}

// ── Drawing helpers ───────────────────────────────────────────────────────────

void FileBrowser::drawFrame(Renderer& renderer,
                            int x, int y, int w, int h,
                            const std::string& title)
{
    renderer.drawShadow(x, y, w, h);
    renderer.drawBoxWithTitle(x, y, w, h,
                              Renderer::CP_DIALOG, Renderer::DOUBLE,
                              " " + title + " ", Renderer::CP_DIALOG_TITLE, A_BOLD);

    wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
    for (int i = 1; i < h - 1; ++i)
        mvwaddstr(stdscr, y + i, x + 1, std::string(w - 2, ' ').c_str());
    wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
}

void FileBrowser::drawPathHeader(Renderer& renderer,
                                 int x, int y, int w,
                                 const std::string& path)
{
    std::string label = " Path: " + path;
    if ((int)label.size() > w - 4)
        label = " Path: ..." + label.substr(label.size() - (w - 10));
    renderer.drawText(x + 1, y + 1, std::string(w - 2, ' '), Renderer::CP_HIGHLIGHT);
    renderer.drawText(x + 2, y + 1, label, Renderer::CP_HIGHLIGHT, A_BOLD);
}

void FileBrowser::drawFileList(Renderer& renderer,
                               int x, int y, int w, int h,
                               const std::vector<FileEntry>& entries,
                               int selection, int top,
                               bool focused, bool dirs_only)
{
    // Background
    wattron(stdscr, COLOR_PAIR(Renderer::CP_LIST_BOX));
    for (int i = 0; i < h; ++i)
        mvwaddstr(stdscr, y + i, x, std::string(w, ' ').c_str());
    wattroff(stdscr, COLOR_PAIR(Renderer::CP_LIST_BOX));

    for (int i = 0; i < h; ++i) {
        int idx = top + i;
        if (idx >= (int)entries.size()) break;
        const auto& e = entries[idx];

        // In SELECT_DIR mode dim regular files to make dirs stand out
        bool selectable = dirs_only ? e.is_directory : !e.is_directory
                                                           || e.name == ".." || e.name == ".";
        // Actually in SELECT_DIR we allow navigating into dirs AND selecting them
        // so all entries are shown; files are just greyed out / non-selectable.

        std::string prefix = e.is_directory ? " [DIR] " : " [FIL] ";
        std::string text   = prefix + e.name;
        if (e.is_directory && e.name != "." && e.name != "..") text += "/";

        if ((int)text.size() > w - 2)
            text = text.substr(0, w - 5) + "...";
        else
            text += std::string(w - (int)text.size() - 1, ' ');

        bool is_sel = focused && (idx == selection);
        int  color  = is_sel ? Renderer::CP_MENU_SELECTED : Renderer::CP_LIST_BOX;

        // Dim non-selectable files in SELECT_DIR mode
        int style = 0;
        if (dirs_only && !e.is_directory)
            style = A_DIM;
        else if (e.is_directory && !is_sel)
            style = A_BOLD;

        renderer.drawText(x + 1, y + i, text, color, style);
    }
}

void FileBrowser::drawFileDetails(Renderer& renderer,
                                  int x, int y,
                                  const FileEntry* e)
{
    if (!e) return;
    int fy = y;
    auto field = [&](const std::string& label, const std::string& val) {
        renderer.drawText(x, fy,     label, Renderer::CP_DIALOG);
        renderer.drawText(x + 2, fy + 1, val, Renderer::CP_DIALOG, A_BOLD);
        fy += 2;
    };
    field("Type:",  e->is_directory ? "Dir" : "File");
    field("Owner:", e->owner);
    field("Perms:", formatPermissions(e->permissions));
    if (!e->is_directory)
        field("Size:", formatSize(e->size));
    field("Mod:",   formatTime(e->mod_time).substr(0, 10));
}

void FileBrowser::drawInputField(Renderer& renderer,
                                 int x, int y, int w,
                                 const std::string& label,
                                 const std::string& value,
                                 bool focused)
{
    std::string full_label = " " + label + " ";
    int field_w = w - 2;
    std::string val = value;
    if ((int)(full_label.size() + val.size()) > field_w)
        val = val.substr(val.size() - (field_w - full_label.size()));

    renderer.drawText(x + 1, y, std::string(field_w, ' '), Renderer::CP_LIST_BOX);
    renderer.drawText(x + 1, y, full_label + val,         Renderer::CP_LIST_BOX);

    if (focused) {
        renderer.showCursor();
        renderer.setCursor(x + 1 + (int)full_label.size() + (int)val.size(), y);
    }
}

void FileBrowser::drawButtons(Renderer& renderer,
                              int x, int y, int w,
                              const std::string& ok_text,  bool ok_sel,
                              const std::string& can_text, bool can_sel,
                              bool pressed)
{
    renderer.drawButton(x + w/2 - 12, y, ok_text,  ok_sel,  pressed && ok_sel);
    renderer.drawButton(x + w/2 + 2,  y, can_text, can_sel, pressed && can_sel);
}

// ── Focus constants ───────────────────────────────────────────────────────────
// Shared by all three modes.  SELECT_DIR skips INPUT (focus 1) entirely.
namespace {
constexpr int F_LIST   = 0;
constexpr int F_INPUT  = 1;   // skipped in SELECT_DIR
constexpr int FOKUS_OK     = 2;
constexpr int F_CANCEL = 3;
constexpr int F_COUNT  = 4;
}

// ── Core implementation ───────────────────────────────────────────────────────

std::string FileBrowser::run(Renderer& renderer, Mode mode,
                             const std::string& initial_filename)
{
    // ── Window size ───────────────────────────────────────────────────────────
    int h = std::min(20, std::max(15, renderer.getHeight() - 10));
    int w = std::min(80, std::max(60, renderer.getWidth()  - 20));
    int starty = (renderer.getHeight() - h) / 2;
    int startx = (renderer.getWidth()  - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);
    nodelay(stdscr, FALSE);

    // ── Mode-specific strings ─────────────────────────────────────────────────
    const bool dirs_only   = (mode == Mode::SELECT_DIR);
    const bool has_input   = (mode != Mode::SELECT_DIR);
    const std::string title    = (mode == Mode::OPEN)       ? "Open File"
                              : (mode == Mode::SAVE)       ? "Save File As"
                                                     :                              "Select Directory";
    const std::string ok_text  = (mode == Mode::OPEN)       ? " &Open "
                                : (mode == Mode::SAVE)       ? " &Save "
                                                       :                              " &Select ";
    const std::string can_text = " &Cancel ";
    const std::string input_label = (mode == Mode::SAVE)    ? "Save:" : "File:";

    // ── State ─────────────────────────────────────────────────────────────────
    char cwd_buf[1024];
    getcwd(cwd_buf, sizeof(cwd_buf));
    std::string current_path = cwd_buf;

    std::string filename_buf  = initial_filename;
    std::string result;
    int selection    = 0;
    int top_of_list  = 0;

    // In SELECT_DIR mode skip the input stop; Tab cycles LIST→OK→CANCEL
    int initial_focus = (mode == Mode::SAVE || mode == Mode::OPEN) ? F_INPUT : F_LIST;
    // OPEN starts on the list; SAVE starts on the input; SELECT_DIR on the list
    if (mode == Mode::OPEN) initial_focus = F_LIST;
    int focus        = initial_focus;
    bool pressed     = false;

    // ── Layout constants ──────────────────────────────────────────────────────
    const int list_w       = (w * 3) / 5;
    const int visible_rows = h - (has_input ? 12 : 10);
    const int details_x    = startx + list_w + 2;
    const int input_y      = starty + h - 6;
    const int btn_y        = starty + h - 4;

    // ── Helper: navigate focus, skipping F_INPUT in SELECT_DIR ───────────────
    auto next_focus = [&](int f, int step) -> int {
        int count = F_COUNT;
        do {
            f = (f + step + count) % count;
        } while (!has_input && f == F_INPUT);
        return f;
    };

    // ── Helper: try to enter a directory ─────────────────────────────────────
    auto enter_dir = [&](const std::string& name) {
        if (chdir(name.c_str()) == 0) {
            getcwd(cwd_buf, sizeof(cwd_buf));
            current_path = cwd_buf;
            selection    = 0;
            top_of_list  = 0;
            filename_buf.clear();
        }
    };

    // ── Helper: check if current state is a valid result ─────────────────────
    auto tryAccept = [&]() -> bool {
        if (dirs_only) {
            // SELECT_DIR: accept current directory itself, or
            // a selected directory entry that isn't . or ..
            std::vector<FileEntry> tmp = readDirectory(current_path);
            sortEntries(tmp);
            if (selection < (int)tmp.size()) {
                const auto& sel = tmp[selection];
                if (sel.is_directory && sel.name != "." && sel.name != "..") {
                    result = current_path + "/" + sel.name;
                    return true;
                }
            }
            // Accept current directory
            result = current_path;
            return true;
        }
        // OPEN / SAVE
        if (filename_buf.empty()) return false;
        std::string full = current_path + "/" + filename_buf;
        struct stat st{};
        if (mode == Mode::OPEN) {
            // Must exist and be a regular file
            return (stat(full.c_str(), &st) == 0 && !S_ISDIR(st.st_mode))
                   && (result = full, true);
        } else {
            // SAVE: target must not be an existing directory
            bool is_dir = (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
            return !is_dir && (result = full, true);
        }
    };

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (true) {
        // Re-read directory every frame (handles external changes)
        auto entries = readDirectory(current_path);
        sortEntries(entries);

        // Keep selection in bounds after directory changes
        if (selection >= (int)entries.size()) selection = (int)entries.size() - 1;
        if (selection < 0) selection = 0;

        // ── Draw ──────────────────────────────────────────────────────────────
        drawFrame      (renderer, startx, starty, w, h, title);
        drawPathHeader (renderer, startx, starty, w, current_path);
        drawFileList   (renderer, startx + 1, starty + 3, list_w, visible_rows,
                     entries, selection, top_of_list, focus == F_LIST, dirs_only);

        if (!entries.empty() && selection < (int)entries.size())
            drawFileDetails(renderer, details_x, starty + 3, &entries[selection]);

        if (has_input)
            drawInputField(renderer, startx + 1, input_y, w - 2,
                           input_label, filename_buf, focus == F_INPUT);

        drawButtons(renderer, startx, btn_y, w,
                    ok_text,  focus == FOKUS_OK,
                    can_text, focus == F_CANCEL,
                    pressed);

        if (focus != F_INPUT) renderer.hideCursor();
        renderer.refresh();

        // ── Press animation then exit ─────────────────────────────────────────
        if (pressed) { napms(100); break; }

        // ── Input ─────────────────────────────────────────────────────────────
        wint_t ch = renderer.getChar();

        // ESC / Alt
        if (ch == 27) {
            timeout(50);
            wint_t next = renderer.getChar();
            timeout(-1);
            if (next == ERR) break;   // bare ESC → cancel

            char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(next)));
            // Ok hotkey
            if ((mode == Mode::OPEN   && lower == 'o') ||
                (mode == Mode::SAVE   && lower == 's') ||
                (mode == Mode::SELECT_DIR && lower == 's')) {
                focus = FOKUS_OK;
                if (tryAccept()) { pressed = true; }
            } else if (lower == 'c') {
                focus = F_CANCEL; pressed = true;
            }
            continue;
        }

        switch (ch) {
        // ── Tab / Shift-Tab ───────────────────────────────────────────────────
        case 9:
            focus = next_focus(focus, +1);
            break;
        case KEY_BTAB:
            focus = next_focus(focus, -1);
            break;

        // ── Arrow keys ────────────────────────────────────────────────────────
        case KEY_UP:
            if (focus == F_LIST) {
                if (selection > 0) {
                    --selection;
                    if (selection < top_of_list) top_of_list = selection;
                    if (has_input && !entries[selection].is_directory)
                        filename_buf = entries[selection].name;
                }
            } else if (focus == F_INPUT) {
                focus = F_LIST;
            } else if (focus ==  FOKUS_OK|| focus == F_CANCEL) {
                focus = has_input ? F_INPUT : F_LIST;
            }
            break;

        case KEY_DOWN:
            if (focus == F_LIST) {
                if (selection < (int)entries.size() - 1) {
                    ++selection;
                    if (selection >= top_of_list + visible_rows) ++top_of_list;
                    if (has_input && !entries[selection].is_directory)
                        filename_buf = entries[selection].name;
                } else {
                    focus = next_focus(F_LIST, +1);
                }
            } else if (focus == F_INPUT) {
                focus = FOKUS_OK;
            }
            break;

        case KEY_LEFT:
            if (focus == F_CANCEL) focus = FOKUS_OK;
            break;

        case KEY_RIGHT:
            if (focus == FOKUS_OK) focus = F_CANCEL;
            break;

        // ── Backspace ─────────────────────────────────────────────────────────
        case KEY_BACKSPACE: case 127: case 8:
            if (focus == F_INPUT && !filename_buf.empty()) {
                filename_buf.pop_back();
            } else if (focus == F_LIST) {
                enter_dir("..");
            }
            break;

        // ── Enter ─────────────────────────────────────────────────────────────
        case KEY_ENTER: case 10: case 13:
            if (focus == F_CANCEL) {
                pressed = true;   // cancel — result stays empty
            } else if (focus ==  FOKUS_OK|| focus == F_INPUT) {
                if (tryAccept()) pressed = true;
            } else if (focus == F_LIST && selection < (int)entries.size()) {
                auto& sel = entries[selection];
                if (sel.is_directory) {
                    enter_dir(sel.name);
                } else if (!dirs_only) {
                    filename_buf = sel.name;
                    focus = F_INPUT;
                }
            }
            break;

        // ── Printable: type into filename field ───────────────────────────────
        default:
            if (has_input && focus == F_INPUT && ch > 31 && ch < KEY_MIN)
                filename_buf += wchar_to_utf8(ch);
            break;
        }
    }

    // ── Restore ───────────────────────────────────────────────────────────────
    copywin(behind, stdscr, 0, 0, starty, startx, starty + h, startx + w, FALSE);
    delwin(behind);
    nodelay(stdscr, TRUE);
    renderer.showCursor();
    return result;
}
