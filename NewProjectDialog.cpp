#include "NewProjectDialog.h"
#include "FileBrowser.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <ncurses.h>
#include <unistd.h>

// ── UTF-8 helpers ─────────────────────────────────────────────────────────────

static void insertUtf8At(std::string& buf, int& pos, wint_t ch)
{
    std::string enc;
    if      (ch < 0x80)    { enc += static_cast<char>(ch); }
    else if (ch < 0x800)   { enc += static_cast<char>(0xC0 | (ch >> 6));
                              enc += static_cast<char>(0x80 | (ch & 0x3F)); }
    else if (ch < 0x10000) { enc += static_cast<char>(0xE0 | (ch >> 12));
                              enc += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                              enc += static_cast<char>(0x80 | (ch & 0x3F)); }
    else                   { enc += static_cast<char>(0xF0 | (ch >> 18));
                              enc += static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
                              enc += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                              enc += static_cast<char>(0x80 | (ch & 0x3F)); }
    buf.insert(pos, enc);
    pos += (int)enc.size();
}

static void eraseUtf8Before(std::string& buf, int& pos)
{
    if (pos <= 0) return;
    int p = pos - 1;
    while (p > 0 && (static_cast<unsigned char>(buf[p]) & 0xC0) == 0x80) --p;
    buf.erase(p, pos - p);
    pos = p;
}

static void eraseUtf8At(std::string& buf, int pos)
{
    if (pos < 0 || pos >= (int)buf.size()) return;
    int end = pos + 1;
    while (end < (int)buf.size() && (static_cast<unsigned char>(buf[end]) & 0xC0) == 0x80) ++end;
    buf.erase(pos, end - pos);
}

static int utf8StepLeft(const std::string& buf, int pos)
{
    if (pos <= 0) return 0;
    int p = pos - 1;
    while (p > 0 && (static_cast<unsigned char>(buf[p]) & 0xC0) == 0x80) --p;
    return p;
}

static int utf8StepRight(const std::string& buf, int pos)
{
    if (pos >= (int)buf.size()) return pos;
    int p = pos + 1;
    while (p < (int)buf.size() && (static_cast<unsigned char>(buf[p]) & 0xC0) == 0x80) ++p;
    return p;
}

static void adjustScroll(int cursor, int field_w, int& scroll)
{
    if (cursor < scroll) scroll = cursor;
    if (cursor > scroll + field_w - 1) scroll = cursor - field_w + 1;
    if (scroll < 0) scroll = 0;
}

// ── loadLibraries ──────────────────────────────────────────────────────────────

std::vector<LibraryInfo> NewProjectDialog::loadLibraries()
{
    namespace fs = std::filesystem;

    // Locate librarian.py: next to executable, then standard install dirs, then cwd
    std::string script;

    auto try_path = [&](const fs::path& p) {
        if (script.empty() && fs::exists(p))
            script = p.string();
    };

    char exe_buf[PATH_MAX] = {};
    if (readlink("/proc/self/exe", exe_buf, PATH_MAX - 1) > 0) {
        fs::path exe_dir = fs::path(exe_buf).parent_path();
        try_path(exe_dir / "librarian.py");
        try_path(exe_dir / "../share/gedi/librarian.py");
    }
    try_path("/usr/share/gedi/librarian.py");
    try_path("/usr/local/share/gedi/librarian.py");
    try_path("librarian.py");

    if (script.empty()) return {};

    FILE* fp = popen(("python3 " + script + " 2>/dev/null").c_str(), "r");
    if (!fp) return {};

    std::string output;
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp))
        output += buf;
    pclose(fp);

    try {
        using json = nlohmann::json;
        auto j = json::parse(output);
        if (!j.is_array()) return {};

        std::vector<LibraryInfo> libs;
        libs.reserve(j.size());
        for (const auto& item : j) {
            LibraryInfo li;
            li.short_name              = item.value("short_name", "");
            li.version                 = item.value("version", "");
            li.description             = item.value("description", "");
            li.cmake_find_package_hint = item.value("cmake_find_package_hint", "");
            li.include_directories     = item.value("include_directories",
                                                     std::vector<std::string>{});
            li.link_libraries          = item.value("link_libraries",
                                                     std::vector<std::string>{});
            li.link_directories        = item.value("link_directories",
                                                     std::vector<std::string>{});
            li.compiler_flags          = item.value("compiler_flags",
                                                     std::vector<std::string>{});
            if (!li.short_name.empty())
                libs.push_back(std::move(li));
        }
        return libs;
    } catch (...) {
        return {};
    }
}

// ── rebuildFilter ─────────────────────────────────────────────────────────────

void NewProjectDialog::rebuildFilter()
{
    m_lib_filtered.clear();
    for (int i = 0; i < (int)m_libraries.size(); ++i) {
        if (m_lib_filter.empty()) {
            m_lib_filtered.push_back(i);
        } else {
            const auto& lib = m_libraries[i];
            auto contains_ci = [&](const std::string& hay) {
                return std::search(hay.begin(), hay.end(),
                                   m_lib_filter.begin(), m_lib_filter.end(),
                                   [](char a, char b) {
                                       return std::tolower((unsigned char)a) ==
                                              std::tolower((unsigned char)b);
                                   }) != hay.end();
            };
            if (contains_ci(lib.short_name) || contains_ci(lib.description))
                m_lib_filtered.push_back(i);
        }
    }
    // Clamp cursor / scroll to the new filtered set
    const int count = (int)m_lib_filtered.size();
    if (m_lib_cursor >= count) m_lib_cursor = std::max(0, count - 1);
    if (m_lib_scroll > m_lib_cursor) m_lib_scroll = m_lib_cursor;
    if (count > 0 && m_lib_cursor >= m_lib_scroll + LIB_VISIBLE)
        m_lib_scroll = m_lib_cursor - LIB_VISIBLE + 1;
    if (m_lib_scroll < 0) m_lib_scroll = 0;
}

// ═══════════════════════════════════════════════════════════════════════════════

NewProjectDialog::NewProjectDialog(Renderer& renderer, ProjectTemplate& t)
    : DialogBase("New Project", W, H)
    , renderer_    (renderer)
    , m_template   (t)
    , name_cursor_ ((int)t.name.size())
    , name_scroll_ (0)
    , path_cursor_ ((int)t.path.size())
    , path_scroll_ (0)
    , bs_cursor_   (t.build_system)
    , standards_   ({"c++11", "c++14", "c++17", "c++20", "c++23", "c++26"})
    , std_idx_     (0)
{
    for (int i = 0; i < (int)standards_.size(); ++i)
        if (standards_[i] == t.cpp_standard) { std_idx_ = i; break; }

    cfg_combo_ = ComboBox(standards_, std_idx_,
                          /*x=*/18, /*y=*/CFG_BOX_Y + 2, /*w=*/12);

    adjustScroll(name_cursor_, FIELD_W_NAME, name_scroll_);
    adjustScroll(path_cursor_, FIELD_W_PATH, path_scroll_);
}

// ── Static factory ────────────────────────────────────────────────────────────

bool NewProjectDialog::show(Renderer& renderer, ProjectTemplate& out_template,
                            const std::vector<LibraryInfo>& libs)
{
    NewProjectDialog dlg(renderer, out_template);
    dlg.m_libraries    = libs;
    dlg.m_lib_selected.assign(dlg.m_libraries.size(), false);
    dlg.rebuildFilter();

    DialogResult res = dlg.run(renderer);

    if (res.accepted()) {
        out_template.selected_libraries.clear();
        for (size_t i = 0; i < dlg.m_libraries.size(); ++i)
            if (dlg.m_lib_selected[i])
                out_template.selected_libraries.push_back(dlg.m_libraries[i]);
    }

    return res.accepted();
}

// ── onInit ────────────────────────────────────────────────────────────────────

void NewProjectDialog::onInit()
{
    // ── Group 0: Project Name ─────────────────────────────────────────────────
    {
        FocusGroup g;
        g.title                = " Project Name ";
        g.box_x                = 2; g.box_y = NAME_BOX_Y;
        g.box_w                = W - 4 - (LIB_BOX_W + 2); g.box_h = NAME_BOX_H;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    // ── Group 1: Location ─────────────────────────────────────────────────────
    {
        FocusGroup g;
        g.title                = " Location ";
        g.box_x                = 2; g.box_y = PATH_BOX_Y;
        g.box_w                = W - 4 - (LIB_BOX_W + 2); g.box_h = PATH_BOX_H;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    // ── Group 2: Configuration ────────────────────────────────────────────────
    {
        FocusGroup g;
        g.title                = " Configuration ";
        g.box_x                = 2; g.box_y = CFG_BOX_Y;
        g.box_w                = W - 4 - (LIB_BOX_W + 2); g.box_h = CFG_BOX_H;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    // ── Group 3: Libraries ────────────────────────────────────────────────────
    {
        FocusGroup g;
        g.title                = " Libraries ";
        g.box_x                = LIB_BOX_X; g.box_y = LIB_BOX_Y;
        g.box_w                = LIB_BOX_W; g.box_h = LIB_BOX_H;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    // ── Button row ────────────────────────────────────────────────────────────
    addButtons(ButtonRow{
        .buttons = {
            Button{
                .label = " C&reate ",
                .x = BTN_CREATE_X, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    if (m_template.name.empty())
                        return HandleResult::CONTINUE;
                    m_template.cpp_standard = cfg_combo_.selectedText();
                    result().accept();
                    return HandleResult::CLOSE;
                }
            },
            Button{
                .label = " &Cancel ",
                .x = BTN_CANCEL_X, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    result().cancel();
                    return HandleResult::CLOSE;
                }
            },
            Button{
                .label = " &Browse ",
                .x = BROWSE_BTN_X, .y = BROWSE_BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    std::string chosen = FileBrowser::selectDirectory(renderer_);
                    if (!chosen.empty()) {
                        m_template.path = chosen;
                        path_cursor_ = (int)chosen.size();
                        path_scroll_ = 0;
                        adjustScroll(path_cursor_, FIELD_W_PATH, path_scroll_);
                    }
                    return HandleResult::CONTINUE;
                }
            },
        }
    });

    setGroupFocus(GRP_NAME);
    setGroupBtnFocus(0);
}

// ── onDraw ────────────────────────────────────────────────────────────────────

void NewProjectDialog::onDraw(Renderer& renderer, int startx, int starty)
{
    const int inner_x = startx + 2;

    // ── Name field ────────────────────────────────────────────────────────────
    {
        const int fy = starty + NAME_BOX_Y + 1;
        adjustScroll(name_cursor_, FIELD_W_NAME, name_scroll_);

        renderer.drawText(inner_x + 2, fy, "Name:", Renderer::CP_DIALOG);
        renderer.drawText(inner_x + FIELD_X, fy,
                          std::string(FIELD_W_NAME, ' '), Renderer::CP_LIST_BOX);

        if ((int)m_template.name.size() > name_scroll_) {
            std::string disp = m_template.name.substr(name_scroll_, FIELD_W_NAME);
            renderer.drawText(inner_x + FIELD_X, fy, disp, Renderer::CP_LIST_BOX);
        }
    }

    // ── Path field ────────────────────────────────────────────────────────────
    {
        const int  fy          = starty + PATH_BOX_Y + 1;
        const bool grp_focused = (getFocusedGroup() == GRP_PATH);
        const int  inner_focus = groups()[GRP_PATH].inner_focus;
        adjustScroll(path_cursor_, FIELD_W_PATH, path_scroll_);

        renderer.drawText(inner_x + 2, fy, "Path:", Renderer::CP_DIALOG);
        renderer.drawText(inner_x + FIELD_X, fy,
                          std::string(FIELD_W_PATH, ' '), Renderer::CP_LIST_BOX);

        if ((int)m_template.path.size() > path_scroll_) {
            std::string disp = m_template.path.substr(path_scroll_, FIELD_W_PATH);
            renderer.drawText(inner_x + FIELD_X, fy, disp, Renderer::CP_LIST_BOX);
        }

        // Checkbox row — below the Browse button shadow row
        const int fy3      = starty + PATH_BOX_Y + 3;
        const bool chk_focused = grp_focused && (inner_focus == 1);
        renderer.drawText(inner_x + 2, fy3,
                          std::string(m_template.create_project_dir ? "[X]" : "[ ]")
                              + " Create project directory",
                          chk_focused ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
    }

    // ── Configuration ─────────────────────────────────────────────────────────
    {
        const int fy         = starty + CFG_BOX_Y + 1;
        bool      grp_focused = (getFocusedGroup() == GRP_CFG);
        int       inner_focus = groups()[GRP_CFG].inner_focus;

        // Row 1: Build system radios
        renderer.drawText(inner_x + 2, fy, "Build system:", Renderer::CP_DIALOG);
        const char* bs_labels[] = {"CMake", "Make", "Meson"};
        int rx = inner_x + 16;
        for (int i = 0; i < 3; ++i) {
            bool is_selected = (m_template.build_system == i);
            bool is_cursor   = grp_focused && (inner_focus == 0) && (bs_cursor_ == i);
            std::string mark = is_selected ? "(•)" : "( )";
            renderer.drawText(rx, fy, mark + " " + bs_labels[i],
                              is_cursor ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
            rx += 4 + (int)strlen(bs_labels[i]) + 1;  // mark(3) + space + label + gap
        }

        // Row 2: C++ Standard combo
        const int fy2 = starty + CFG_BOX_Y + 2;
        renderer.drawText(inner_x + 2, fy2, "C++ Standard:", Renderer::CP_DIALOG);
        cfg_combo_.draw(renderer, startx, starty,
                        grp_focused && (inner_focus == 1));

        // Row 3: Checkboxes
        const int fy3 = starty + CFG_BOX_Y + 3;
        const bool git_focused  = grp_focused && (inner_focus == 2) && (cfg_chk_cursor_ == 0);
        const bool main_focused = grp_focused && (inner_focus == 2) && (cfg_chk_cursor_ == 1);

        renderer.drawText(inner_x + 2, fy3,
                          std::string(m_template.init_git   ? "[X]" : "[ ]") + " Initialize git",
                          git_focused  ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
        renderer.drawText(inner_x + 22, fy3,
                          std::string(m_template.create_main ? "[X]" : "[ ]") + " Create main.cpp",
                          main_focused ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
    }

    // ── Library list ──────────────────────────────────────────────────────────
    {
        const int lib_x   = startx + LIB_BOX_X + 1;
        const int lib_y0  = starty + LIB_BOX_Y + 1;
        bool grp_focused  = (getFocusedGroup() == GRP_LIB);

        // Filter line (first inner row)
        {
            std::string fline = "/ " + m_lib_filter;
            while ((int)fline.size() < LIB_ITEM_W) fline += ' ';
            fline = fline.substr(0, LIB_ITEM_W);
            renderer.drawText(lib_x, lib_y0, fline,
                              grp_focused ? Renderer::CP_LIST_BOX : Renderer::CP_DIALOG);
        }

        // Library items (rows below the filter line)
        if (m_libraries.empty()) {
            renderer.drawText(lib_x, lib_y0 + 1, "No libraries found",
                              Renderer::CP_DIALOG);
        } else if (m_lib_filtered.empty()) {
            renderer.drawText(lib_x, lib_y0 + 1, "No matches",
                              Renderer::CP_DIALOG);
        } else {
            for (int row = 0; row < LIB_VISIBLE; ++row) {
                int filt_idx = m_lib_scroll + row;
                if (filt_idx >= (int)m_lib_filtered.size()) break;
                int real_idx = m_lib_filtered[filt_idx];

                const auto& lib = m_libraries[real_idx];
                std::string check = m_lib_selected[real_idx] ? "[X]" : "[ ]";

                std::string text = lib.short_name;
                if (!lib.version.empty())
                    text += " " + lib.version;
                if (!lib.description.empty())
                    text += " " + lib.description;

                const int max_text = LIB_ITEM_W - 4;
                if ((int)text.size() > max_text)
                    text = text.substr(0, max_text);

                std::string line = check + " " + text;
                while ((int)line.size() < LIB_ITEM_W) line += ' ';

                bool is_cursor = grp_focused && (filt_idx == m_lib_cursor);
                int color = is_cursor ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG;
                renderer.drawText(lib_x, lib_y0 + 1 + row, line, color);
            }
        }
    }
}

// ── onPlaceCursor ─────────────────────────────────────────────────────────────

bool NewProjectDialog::onPlaceCursor(Renderer& renderer, int sx, int sy)
{
    int grp = getFocusedGroup();

    if (grp == GRP_NAME) {
        renderer.showCursor();
        move(sy + NAME_BOX_Y + 1,
             sx + 2 + FIELD_X + (name_cursor_ - name_scroll_));
        return true;
    }

    if (grp == GRP_PATH) {
        if (groups()[GRP_PATH].inner_focus == 0) {
            renderer.showCursor();
            move(sy + PATH_BOX_Y + 1,
                 sx + 2 + FIELD_X + (path_cursor_ - path_scroll_));
            return true;
        }
        return false;  // checkbox row: highlight only, no text cursor
    }

    if (grp == GRP_LIB) {
        renderer.showCursor();
        int cx = sx + LIB_BOX_X + 1 + 2 + (int)m_lib_filter.size();
        int max_cx = sx + LIB_BOX_X + LIB_ITEM_W;
        move(sy + LIB_BOX_Y + 1, std::min(cx, max_cx));
        return true;
    }

    return false;
}

// ── onKey ─────────────────────────────────────────────────────────────────────

HandleResult NewProjectDialog::onKey(wint_t ch)
{
    int grp = getFocusedGroup();

    // ── Name field ────────────────────────────────────────────────────────────
    if (grp == GRP_NAME) {
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            eraseUtf8Before(m_template.name, name_cursor_);
        } else if (ch == KEY_DC) {
            eraseUtf8At(m_template.name, name_cursor_);
        } else if (ch == KEY_LEFT) {
            name_cursor_ = utf8StepLeft(m_template.name, name_cursor_);
        } else if (ch == KEY_RIGHT) {
            name_cursor_ = utf8StepRight(m_template.name, name_cursor_);
        } else if (ch == KEY_HOME) {
            name_cursor_ = 0;
        } else if (ch == KEY_END) {
            name_cursor_ = (int)m_template.name.size();
        } else if (ch > 31 && ch < KEY_MIN) {
            insertUtf8At(m_template.name, name_cursor_, ch);
        }
        return HandleResult::CONTINUE;
    }

    // ── Path field ────────────────────────────────────────────────────────────
    if (grp == GRP_PATH) {
        auto& g = groups()[GRP_PATH];

        if (ch == KEY_UP || ch == KEY_DOWN) {
            g.inner_focus = (g.inner_focus == 0) ? 1 : 0;
            return HandleResult::CONTINUE;
        }

        if (g.inner_focus == 0) {
            if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                eraseUtf8Before(m_template.path, path_cursor_);
            } else if (ch == KEY_DC) {
                eraseUtf8At(m_template.path, path_cursor_);
            } else if (ch == KEY_LEFT) {
                path_cursor_ = utf8StepLeft(m_template.path, path_cursor_);
            } else if (ch == KEY_RIGHT) {
                path_cursor_ = utf8StepRight(m_template.path, path_cursor_);
            } else if (ch == KEY_HOME) {
                path_cursor_ = 0;
            } else if (ch == KEY_END) {
                path_cursor_ = (int)m_template.path.size();
            } else if (ch > 31 && ch < KEY_MIN) {
                insertUtf8At(m_template.path, path_cursor_, ch);
            }
        } else {
            if (ch == ' ')
                m_template.create_project_dir = !m_template.create_project_dir;
        }
        return HandleResult::CONTINUE;
    }

    // ── Configuration group ───────────────────────────────────────────────────
    if (grp == GRP_CFG) {
        auto& g = groups()[GRP_CFG];

        if (ch == KEY_UP || ch == KEY_DOWN) {
            if (ch == KEY_DOWN) g.inner_focus = (g.inner_focus + 1) % 3;
            else                g.inner_focus = (g.inner_focus == 0) ? 2 : g.inner_focus - 1;
            return HandleResult::CONTINUE;
        }

        if (g.inner_focus == 0) {
            if (ch == KEY_LEFT  && bs_cursor_ > 0)
            { --bs_cursor_; m_template.build_system = bs_cursor_; return HandleResult::CONTINUE; }
            if (ch == KEY_RIGHT && bs_cursor_ < 2)
            { ++bs_cursor_; m_template.build_system = bs_cursor_; return HandleResult::CONTINUE; }
        } else if (g.inner_focus == 1) {
            cfg_combo_.handleKey(ch);
        } else {
            if (ch == KEY_LEFT  && cfg_chk_cursor_ > 0) { --cfg_chk_cursor_; return HandleResult::CONTINUE; }
            if (ch == KEY_RIGHT && cfg_chk_cursor_ < 1) { ++cfg_chk_cursor_; return HandleResult::CONTINUE; }
            if (ch == ' ') {
                if (cfg_chk_cursor_ == 0) m_template.init_git    = !m_template.init_git;
                else                       m_template.create_main = !m_template.create_main;
                return HandleResult::CONTINUE;
            }
        }
    }

    // ── Library list ──────────────────────────────────────────────────────────
    if (grp == GRP_LIB) {
        const int count = (int)m_lib_filtered.size();

        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!m_lib_filter.empty()) {
                m_lib_filter.pop_back();
                rebuildFilter();
            }
        } else if (ch == KEY_UP) {
            if (m_lib_cursor > 0) {
                --m_lib_cursor;
                if (m_lib_cursor < m_lib_scroll) m_lib_scroll = m_lib_cursor;
            }
        } else if (ch == KEY_DOWN) {
            if (m_lib_cursor < count - 1) {
                ++m_lib_cursor;
                if (m_lib_cursor >= m_lib_scroll + LIB_VISIBLE)
                    m_lib_scroll = m_lib_cursor - LIB_VISIBLE + 1;
            }
        } else if (ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13) {
            if (!m_lib_filtered.empty()) {
                int real_idx = m_lib_filtered[m_lib_cursor];
                m_lib_selected[real_idx] = !m_lib_selected[real_idx];
            }
        } else if (ch == KEY_PPAGE) {
            m_lib_cursor = std::max(0, m_lib_cursor - LIB_VISIBLE);
            m_lib_scroll = std::max(0, m_lib_scroll - LIB_VISIBLE);
        } else if (ch == KEY_NPAGE) {
            if (count > 0) {
                m_lib_cursor = std::min(count - 1, m_lib_cursor + LIB_VISIBLE);
                if (m_lib_cursor >= m_lib_scroll + LIB_VISIBLE)
                    m_lib_scroll = m_lib_cursor - LIB_VISIBLE + 1;
            }
        } else if (ch > 31 && ch < KEY_MIN) {
            m_lib_filter += static_cast<char>(ch);
            rebuildFilter();
        }
        return HandleResult::CONTINUE;
    }

    return HandleResult::CONTINUE;
}

// ── onTab ─────────────────────────────────────────────────────────────────────
// Custom Tab cycle:
//   Forward:  Name → Path → Browse → Config → Lib → Create → Cancel → Name
//   Backward: Name → Cancel → Create → Lib → Config → Browse → Path → Name

bool NewProjectDialog::onTab(bool forward)
{
    const int btn_row = groupCount();   // = 4; groups_.size()

    if (forward) {
        // Path → Browse
        if (getFocusedGroup() == GRP_PATH) {
            setGroupFocus(btn_row);
            setGroupBtnFocus(BTN_IDX_BROWSE);
            return true;
        }
        // Browse → Config
        if (inButtonRow() && getBtnInnerFocus() == BTN_IDX_BROWSE) {
            setGroupFocus(GRP_CFG);
            return true;
        }
        // Cancel → Name  (skip over Browse in the default row cycle)
        if (inButtonRow() && getBtnInnerFocus() == BTN_IDX_CANCEL) {
            setGroupFocus(GRP_NAME);
            return true;
        }
    } else {
        // Config → Browse
        if (getFocusedGroup() == GRP_CFG) {
            setGroupFocus(btn_row);
            setGroupBtnFocus(BTN_IDX_BROWSE);
            return true;
        }
        // Browse → Path
        if (inButtonRow() && getBtnInnerFocus() == BTN_IDX_BROWSE) {
            setGroupFocus(GRP_PATH);
            return true;
        }
        // Name → Cancel  (skip over Browse in the default row cycle)
        if (getFocusedGroup() == GRP_NAME) {
            setGroupFocus(btn_row);
            setGroupBtnFocus(BTN_IDX_CANCEL);
            return true;
        }
    }

    return false;   // let DialogBase handle the rest
}
