#include "FileBrowser.h"
#include "utils.h"

#include <ncurses.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fnmatch.h>
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

std::string FileBrowser::open(Renderer& renderer,
                              const std::string& title,
                              const std::vector<FilterEntry>& filters)
{
    return run(renderer, Mode::OPEN, "", title, filters);
}

std::string FileBrowser::save(Renderer& renderer,
                              const std::string& current_filename,
                              const std::vector<FilterEntry>& filters)
{
    std::string initial;
    auto slash = current_filename.find_last_of('/');
    initial = (slash != std::string::npos)
                  ? current_filename.substr(slash + 1)
                  : current_filename;
    return run(renderer, Mode::SAVE, initial, "Save File As", filters);
}

std::string FileBrowser::selectDirectory(Renderer& renderer)
{
    return run(renderer, Mode::SELECT_DIR, "", "Select Directory", {});
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
    int max_len = w - 4;
    std::string label = " Path: " + path;
    if ((int)label.size() > max_len)
        label = " Path: ..." + path.substr(path.size() - (max_len - 10));
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

        // Directories: "/name", files: " name" (leading space for alignment)
        std::string text = (e.is_directory ? "/" : " ") + e.name;

        int max_text = w - 2;
        if ((int)text.size() > max_text)
            text = text.substr(0, max_text - 3) + "...";
        else
            text += std::string(max_text - (int)text.size(), ' ');

        bool is_sel = focused && (idx == selection);
        int color, style;
        if (is_sel) {
            color = Renderer::CP_MENU_SELECTED;
            style = e.is_directory ? A_BOLD : A_NORMAL;
        } else if (e.is_directory) {
            color = Renderer::CP_DIALOG_TITLE;
            style = A_BOLD;
        } else if (dirs_only) {
            color = Renderer::CP_LIST_BOX;
            style = A_DIM;
        } else {
            color = Renderer::CP_LIST_BOX;
            style = A_NORMAL;
        }

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

void FileBrowser::drawFilterCombo(Renderer& renderer,
                                   int x, int y, int w,
                                   const std::vector<FilterEntry>& filters,
                                   int idx, bool focused)
{
    // Prefix " Type: [ " — 9 ASCII chars → 9 display columns.
    // Suffix " ▼ ]"      — 6 bytes (▼ = \xe2\x96\xbc), 4 display columns.
    // Suffix "   ]"      — 4 bytes, 4 display columns.
    // Use display-column counts for layout, not byte sizes.
    static constexpr int PREFIX_COLS = 9;
    static constexpr int SUFFIX_COLS = 4;
    static const std::string prefix       = " Type: [ ";
    static const std::string suffix_multi = " \xe2\x96\xbc ]";   // " ▼ ]"
    static const std::string suffix_one   = "   ]";

    const bool        multi   = (filters.size() > 1);
    const std::string& suffix = multi ? suffix_multi : suffix_one;

    int content_w = (w - 2) - PREFIX_COLS - SUFFIX_COLS;
    if (content_w < 4) content_w = 4;

    std::string label = (idx < (int)filters.size()) ? filters[idx].label : "";
    if ((int)label.size() > content_w)
        label = label.substr(0, content_w - 3) + "...";
    label += std::string(content_w - (int)label.size(), ' ');

    int color = focused ? Renderer::CP_MENU_SELECTED : Renderer::CP_LIST_BOX;
    renderer.drawText(x + 1,                        y, prefix, Renderer::CP_DIALOG);
    renderer.drawText(x + 1 + PREFIX_COLS,          y, label,  color);
    renderer.drawText(x + 1 + PREFIX_COLS + content_w, y, suffix, Renderer::CP_DIALOG);
}

// ── Focus constants ───────────────────────────────────────────────────────────
// FB_FILTER is skipped when there is no filter combo (!has_filter_combo).
// FB_INPUT  is skipped in SELECT_DIR mode.
namespace {
constexpr int FB_LIST   = 0;
constexpr int FB_INPUT  = 1;
constexpr int FB_FILTER = 2;
constexpr int FB_OK     = 3;
constexpr int FB_CANCEL = 4;
constexpr int FB_COUNT  = 5;
}

// ── Core implementation ───────────────────────────────────────────────────────

std::string FileBrowser::run(Renderer& renderer, Mode mode,
                             const std::string& initial_filename,
                             const std::string& title,
                             const std::vector<FilterEntry>& filters)
{
    // ── Mode flags ────────────────────────────────────────────────────────────
    const bool dirs_only        = (mode == Mode::SELECT_DIR);
    const bool has_input        = (mode != Mode::SELECT_DIR);
    const bool has_filter_combo = !filters.empty() && has_input;

    // ── Window size ───────────────────────────────────────────────────────────
    // Base size; add 2 rows when filter combo is shown so the list keeps its depth.
    int h = std::min(20, std::max(16, renderer.getHeight() - 10));
    if (has_filter_combo)
        h = std::min(h + 2, renderer.getHeight() - 2);
    int w = std::min(66, std::max(58, renderer.getWidth() - 20));
    int starty = (renderer.getHeight() - h) / 2;
    int startx = (renderer.getWidth()  - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);
    nodelay(stdscr, FALSE);

    // ── Mode-specific strings ─────────────────────────────────────────────────
    const std::string ok_text     = (mode == Mode::OPEN) ? " &Open "
                                  : (mode == Mode::SAVE) ? " &Save " : " &Select ";
    const std::string can_text    = " &Cancel ";
    const std::string input_label = (mode == Mode::SAVE) ? "Save:" : "File:";

    // ── State ─────────────────────────────────────────────────────────────────
    char cwd_buf[1024];
    getcwd(cwd_buf, sizeof(cwd_buf));
    std::string current_path = cwd_buf;

    std::string filename_buf = initial_filename;
    std::string result;
    int selection   = 0;
    int top_of_list = 0;
    int filter_idx  = 0;   // active filter in the combo

    int focus = (mode == Mode::OPEN) ? FB_LIST
              : (mode == Mode::SAVE) ? FB_INPUT
                                     : FB_LIST;
    bool pressed = false;

    // ── Layout ───────────────────────────────────────────────────────────────
    // Fixed overhead per row-group:
    //   borders(2) + path(1) + gap(1) + gap-after-list(1) + btn(1) + gap(1) = 7
    //   + input(1) + gap(1)  = 2 if has_input
    //   + filter(1) + gap(1) = 2 if has_filter_combo
    const int overhead     = 7 + (has_input ? 2 : 0) + (has_filter_combo ? 2 : 0);
    const int visible_rows = h - overhead;
    const int list_w       = (w * 3) / 5;
    const int details_x    = startx + list_w + 2;
    const int list_end_y   = starty + 3 + visible_rows;      // first row after list
    const int input_y      = list_end_y + 1;                  // only when has_input
    const int filter_y     = has_filter_combo ? input_y + 2 : -1;
    const int btn_y        = starty + h - 3;

    // ── Helpers ───────────────────────────────────────────────────────────────
    auto next_focus = [&](int f, int step) -> int {
        do {
            f = (f + step + FB_COUNT) % FB_COUNT;
        } while ((!has_input        && f == FB_INPUT)  ||
                 (!has_filter_combo && f == FB_FILTER));
        return f;
    };

    auto sync_filename = [&](const std::vector<FileEntry>& ents) {
        if (has_input && selection < (int)ents.size() && !ents[selection].is_directory)
            filename_buf = ents[selection].name;
    };

    auto enter_dir = [&](const std::string& name) {
        if (chdir(name.c_str()) == 0) {
            getcwd(cwd_buf, sizeof(cwd_buf));
            current_path = cwd_buf;
            selection    = 0;
            top_of_list  = 0;
            filename_buf.clear();
        }
    };

    // Active glob pattern from current filter (empty = all files).
    auto activePattern = [&]() -> const std::string& {
        static const std::string empty;
        if (!has_filter_combo || filter_idx >= (int)filters.size()) return empty;
        return filters[filter_idx].pattern;
    };

    auto tryAccept = [&]() -> bool {
        if (dirs_only) {
            std::vector<FileEntry> tmp = readDirectory(current_path);
            sortEntries(tmp);
            if (selection < (int)tmp.size()) {
                const auto& sel = tmp[selection];
                if (sel.is_directory && sel.name != "." && sel.name != "..") {
                    result = current_path + "/" + sel.name;
                    return true;
                }
            }
            result = current_path;
            return true;
        }
        if (filename_buf.empty()) return false;
        std::string full = current_path + "/" + filename_buf;
        struct stat st{};
        if (mode == Mode::OPEN) {
            return (stat(full.c_str(), &st) == 0 && !S_ISDIR(st.st_mode))
                   && (result = full, true);
        } else {
            bool is_dir = (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
            return !is_dir && (result = full, true);
        }
    };

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (true) {
        auto entries = readDirectory(current_path);
        sortEntries(entries);

        // Apply active filter (dirs always shown; supports semicolon-separated globs)
        const std::string& pat = activePattern();
        if (!pat.empty() && pat != "*") {
            entries.erase(
                std::remove_if(entries.begin(), entries.end(),
                    [&](const FileEntry& e) {
                        if (e.is_directory) return false;
                        // Keep file if it matches any semicolon-delimited pattern
                        const char* p   = pat.c_str();
                        const char* end = p + pat.size();
                        while (p < end) {
                            const char* semi = std::find(p, end, ';');
                            std::string single(p, semi);
                            if (!single.empty() &&
                                fnmatch(single.c_str(), e.name.c_str(), 0) == 0)
                                return false;
                            p = (semi < end) ? semi + 1 : end;
                        }
                        return true;
                    }),
                entries.end());
        }

        if (selection >= (int)entries.size()) selection = (int)entries.size() - 1;
        if (selection < 0) selection = 0;

        // ── Draw ──────────────────────────────────────────────────────────────
        drawFrame     (renderer, startx, starty, w, h, title);
        drawPathHeader(renderer, startx, starty, w, current_path);
        drawFileList  (renderer, startx + 1, starty + 3, list_w, visible_rows,
                       entries, selection, top_of_list, focus == FB_LIST, dirs_only);

        if (!entries.empty() && selection < (int)entries.size())
            drawFileDetails(renderer, details_x, starty + 3, &entries[selection]);

        if (has_input)
            drawInputField(renderer, startx + 1, input_y, w - 2,
                           input_label, filename_buf, focus == FB_INPUT);

        if (has_filter_combo)
            drawFilterCombo(renderer, startx, filter_y, w, filters,
                            filter_idx, focus == FB_FILTER);

        drawButtons(renderer, startx, btn_y, w,
                    ok_text,  focus == FB_OK,
                    can_text, focus == FB_CANCEL,
                    pressed);

        if (focus == FB_INPUT && has_input) {
            std::string full_label = " " + input_label + " ";
            int field_w = (w - 2) - 2;
            std::string val = filename_buf;
            if ((int)(full_label.size() + val.size()) > field_w)
                val = val.substr(val.size() - (field_w - full_label.size()));
            renderer.showCursor();
            renderer.setCursor(startx + 2 + (int)full_label.size() + (int)val.size(), input_y);
        } else {
            renderer.hideCursor();
        }
        renderer.refresh();

        if (pressed) { napms(100); break; }

        // ── Input ─────────────────────────────────────────────────────────────
        wint_t ch = renderer.getChar();

        // ESC / Alt-hotkey
        if (ch == 27) {
            timeout(50);
            wint_t next = renderer.getChar();
            timeout(-1);
            if (next == ERR) break;   // bare ESC → cancel
            char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(next)));
            if ((mode == Mode::OPEN       && lower == 'o') ||
                (mode == Mode::SAVE       && lower == 's') ||
                (mode == Mode::SELECT_DIR && lower == 's')) {
                focus = FB_OK;
                if (tryAccept()) pressed = true;
            } else if (lower == 'c') {
                focus = FB_CANCEL; pressed = true;
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
            if (focus == FB_LIST) {
                if (selection > 0) {
                    --selection;
                    if (selection < top_of_list) top_of_list = selection;
                    sync_filename(entries);
                }
            } else if (focus == FB_INPUT) {
                focus = FB_LIST;
            } else if (focus == FB_FILTER) {
                focus = has_input ? FB_INPUT : FB_LIST;
            } else if (focus == FB_OK || focus == FB_CANCEL) {
                focus = next_focus(FB_OK, -1);
            }
            break;

        case KEY_DOWN:
            if (focus == FB_LIST) {
                if (selection < (int)entries.size() - 1) {
                    ++selection;
                    if (selection >= top_of_list + visible_rows) ++top_of_list;
                    sync_filename(entries);
                } else {
                    focus = next_focus(FB_LIST, +1);
                }
            } else if (focus == FB_INPUT) {
                focus = next_focus(FB_INPUT, +1);
            } else if (focus == FB_FILTER) {
                focus = FB_OK;
            }
            break;

        case KEY_LEFT:
            if (focus == FB_CANCEL) {
                focus = FB_OK;
            } else if (focus == FB_FILTER && has_filter_combo) {
                if (filter_idx > 0) { --filter_idx; filename_buf.clear(); }
            }
            break;

        case KEY_RIGHT:
            if (focus == FB_OK) {
                focus = FB_CANCEL;
            } else if (focus == FB_FILTER && has_filter_combo) {
                if (filter_idx < (int)filters.size() - 1) { ++filter_idx; filename_buf.clear(); }
            }
            break;

        // ── Page / Home / End ─────────────────────────────────────────────────
        case KEY_PPAGE:
            if (focus == FB_LIST && !entries.empty()) {
                selection   = std::max(0, selection - visible_rows);
                top_of_list = std::max(0, top_of_list - visible_rows);
                if (selection < top_of_list) top_of_list = selection;
                sync_filename(entries);
            }
            break;

        case KEY_NPAGE:
            if (focus == FB_LIST && !entries.empty()) {
                selection = std::min((int)entries.size() - 1, selection + visible_rows);
                if (selection >= top_of_list + visible_rows)
                    top_of_list = selection - visible_rows + 1;
                sync_filename(entries);
            }
            break;

        case KEY_HOME:
            if (focus == FB_LIST && !entries.empty()) {
                selection = 0; top_of_list = 0;
                sync_filename(entries);
            }
            break;

        case KEY_END:
            if (focus == FB_LIST && !entries.empty()) {
                selection = (int)entries.size() - 1;
                if (selection >= top_of_list + visible_rows)
                    top_of_list = selection - visible_rows + 1;
                sync_filename(entries);
            }
            break;

        // ── Backspace ─────────────────────────────────────────────────────────
        case KEY_BACKSPACE: case 127: case 8:
            if (focus == FB_INPUT && !filename_buf.empty())
                filename_buf.pop_back();
            else if (focus == FB_LIST)
                enter_dir("..");
            break;

        // ── Enter ─────────────────────────────────────────────────────────────
        case KEY_ENTER: case 10: case 13:
            if (focus == FB_CANCEL) {
                pressed = true;
            } else if (focus == FB_OK || focus == FB_INPUT) {
                if (tryAccept()) pressed = true;
            } else if (focus == FB_FILTER) {
                // Enter on combo cycles to next filter (wraps around)
                if (has_filter_combo && filters.size() > 1) {
                    filter_idx = (filter_idx + 1) % (int)filters.size();
                    filename_buf.clear();
                }
            } else if (focus == FB_LIST && selection < (int)entries.size()) {
                auto& sel = entries[selection];
                if (sel.is_directory) {
                    enter_dir(sel.name);
                } else if (!dirs_only) {
                    filename_buf = sel.name;
                    if (tryAccept()) pressed = true;
                }
            }
            break;

        // ── Printable: type into filename field ───────────────────────────────
        default:
            if (has_input && focus == FB_INPUT && ch > 31 && ch < KEY_MIN)
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
