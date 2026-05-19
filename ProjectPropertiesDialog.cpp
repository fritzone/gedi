#include "ProjectPropertiesDialog.h"
#include <algorithm>
#include <filesystem>
#include <ncurses.h>

// ── Constructor ───────────────────────────────────────────────────────────────

ProjectPropertiesDialog::ProjectPropertiesDialog(Renderer& renderer,
                                                 GediProject& project,
                                                 const std::vector<LibraryInfo>& all_libs)
    : DialogBase("Project Properties", W, H)
    , renderer_(renderer)
    , project_(project)
    , standards_({"c++11", "c++14", "c++17", "c++20", "c++23", "c++26"})
    , tgt_list_(project.targets)
    , m_sys_libs_(all_libs)
{
    // Build system radio
    if      (project.build_system == "make")  bs_cursor_ = 1;
    else if (project.build_system == "meson") bs_cursor_ = 2;
    else                                       bs_cursor_ = 0;

    // C++ standard combo
    for (int i = 0; i < (int)standards_.size(); ++i)
        if (standards_[i] == project.compiler_settings.cpp_standard) { std_idx_ = i; break; }
    cfg_combo_ = ComboBox(standards_, std_idx_, /*x=*/18, /*y=*/CFG_BOX_Y + 2, /*w=*/12);

    // Mark which system libs are already used by the project
    m_sys_lib_used_.assign(m_sys_libs_.size(), false);
    for (const auto& proj_lib : project.libraries)
        for (int i = 0; i < (int)m_sys_libs_.size(); ++i)
            if (m_sys_libs_[i].short_name == proj_lib.short_name) {
                m_sys_lib_used_[i] = true; break;
            }

    rebuildLibEntries();
}

// ── Static factory ────────────────────────────────────────────────────────────

bool ProjectPropertiesDialog::show(Renderer& renderer, GediProject& project,
                                   const std::vector<LibraryInfo>& all_libs)
{
    ProjectPropertiesDialog dlg(renderer, project, all_libs);
    DialogResult res = dlg.run(renderer);

    if (res.accepted()) {
        project.targets = dlg.tgt_list_;

        const char* bs_names[] = {"cmake", "make", "meson"};
        project.build_system = bs_names[dlg.bs_cursor_];

        project.compiler_settings.cpp_standard = dlg.cfg_combo_.selectedText();
        project.cpp_standard = project.compiler_settings.cpp_standard;

        project.libraries.clear();
        for (int i = 0; i < (int)dlg.m_sys_libs_.size(); ++i)
            if (dlg.m_sys_lib_used_[i])
                project.libraries.push_back(dlg.m_sys_libs_[i]);
    }

    return res.accepted();
}

// ── rebuildLibEntries ─────────────────────────────────────────────────────────
// Builds the flat visible list used by the library panel. Ordering:
//   1. Project library targets (static_library / shared_library) — not the focused one itself
//   2. System libraries already in the project  (marked [X])
//   3. System libraries not yet in the project  (marked [ ])
// Each section gets a header row only if it has ≥1 matching item.

void ProjectPropertiesDialog::rebuildLibEntries()
{
    auto contains_ci = [&](const std::string& hay) -> bool {
        if (m_lib_filter_.empty()) return true;
        return std::search(hay.begin(), hay.end(),
                           m_lib_filter_.begin(), m_lib_filter_.end(),
                           [](char a, char b){
                               return std::tolower((unsigned char)a) ==
                                      std::tolower((unsigned char)b);
                           }) != hay.end();
    };

    // Remember old cursor label so we can restore position after rebuild
    std::string old_label;
    if (m_lib_cursor_ < (int)m_lib_entries_.size())
        old_label = m_lib_entries_[m_lib_cursor_].label;

    m_lib_entries_.clear();

    // ── Section 1: project library targets ───────────────────────────────────
    {
        std::vector<LibEntry> sec;
        for (int i = 0; i < (int)tgt_list_.size(); ++i) {
            if (i == tgt_cursor_) continue;           // skip self
            const auto& t = tgt_list_[i];
            if (t.type == "executable") continue;     // only lib targets
            std::string abbr = (t.type == "static_library") ? "static" : "shared";
            std::string lbl  = t.name + "  [" + abbr + "]";
            if (contains_ci(t.name))
                sec.push_back({LibEntry::Kind::PROJ_LIB, lbl, i});
        }
        if (!sec.empty()) {
            m_lib_entries_.push_back({LibEntry::Kind::HEADER, "\xe2\x94\x80 Project libraries \xe2\x94\x80", -1});
            for (auto& e : sec) m_lib_entries_.push_back(std::move(e));
        }
    }

    // ── Section 2: system libs in use ────────────────────────────────────────
    {
        std::vector<LibEntry> sec;
        for (int i = 0; i < (int)m_sys_libs_.size(); ++i) {
            if (!m_sys_lib_used_[i]) continue;
            const auto& lib = m_sys_libs_[i];
            std::string lbl = lib.short_name;
            if (!lib.version.empty()) lbl += " " + lib.version;
            if (!lib.description.empty()) lbl += "  " + lib.description;
            if (contains_ci(lib.short_name) || contains_ci(lib.description))
                sec.push_back({LibEntry::Kind::SYS_LIB, lbl, i});
        }
        if (!sec.empty()) {
            m_lib_entries_.push_back({LibEntry::Kind::HEADER, "\xe2\x94\x80 System (in use) \xe2\x94\x80", -1});
            for (auto& e : sec) m_lib_entries_.push_back(std::move(e));
        }
    }

    // ── Section 3: system libs available ─────────────────────────────────────
    {
        std::vector<LibEntry> sec;
        for (int i = 0; i < (int)m_sys_libs_.size(); ++i) {
            if (m_sys_lib_used_[i]) continue;
            const auto& lib = m_sys_libs_[i];
            std::string lbl = lib.short_name;
            if (!lib.version.empty()) lbl += " " + lib.version;
            if (!lib.description.empty()) lbl += "  " + lib.description;
            if (contains_ci(lib.short_name) || contains_ci(lib.description))
                sec.push_back({LibEntry::Kind::SYS_LIB, lbl, i});
        }
        if (!sec.empty()) {
            m_lib_entries_.push_back({LibEntry::Kind::HEADER, "\xe2\x94\x80 System (available) \xe2\x94\x80", -1});
            for (auto& e : sec) m_lib_entries_.push_back(std::move(e));
        }
    }

    // ── Restore / clamp cursor ────────────────────────────────────────────────
    // Try to keep the cursor on the same label; fall back to first selectable row.
    int new_cur = -1;
    for (int i = 0; i < (int)m_lib_entries_.size(); ++i)
        if (m_lib_entries_[i].kind != LibEntry::Kind::HEADER &&
            m_lib_entries_[i].label == old_label) { new_cur = i; break; }

    if (new_cur < 0) {
        // Find first non-header
        for (int i = 0; i < (int)m_lib_entries_.size(); ++i)
            if (m_lib_entries_[i].kind != LibEntry::Kind::HEADER) { new_cur = i; break; }
    }
    m_lib_cursor_ = std::max(0, new_cur);

    // Clamp scroll
    const int n = (int)m_lib_entries_.size();
    if (m_lib_scroll_ > m_lib_cursor_) m_lib_scroll_ = m_lib_cursor_;
    if (n > 0 && m_lib_cursor_ >= m_lib_scroll_ + LIB_VISIBLE)
        m_lib_scroll_ = m_lib_cursor_ - LIB_VISIBLE + 1;
    if (m_lib_scroll_ < 0) m_lib_scroll_ = 0;
}

// ── libEntrySelected / libEntryToggle ─────────────────────────────────────────

bool ProjectPropertiesDialog::libEntrySelected(const LibEntry& e) const
{
    if (e.kind == LibEntry::Kind::PROJ_LIB) {
        if (e.idx < 0 || e.idx >= (int)tgt_list_.size()) return false;
        if (tgt_cursor_ < 0 || tgt_cursor_ >= (int)tgt_list_.size()) return false;
        const auto& links = tgt_list_[tgt_cursor_].link_targets;
        return std::find(links.begin(), links.end(), tgt_list_[e.idx].name) != links.end();
    }
    if (e.kind == LibEntry::Kind::SYS_LIB)
        return e.idx >= 0 && e.idx < (int)m_sys_lib_used_.size() && m_sys_lib_used_[e.idx];
    return false;
}

void ProjectPropertiesDialog::libEntryToggle(const LibEntry& e)
{
    if (e.kind == LibEntry::Kind::PROJ_LIB) {
        if (e.idx < 0 || e.idx >= (int)tgt_list_.size()) return;
        if (tgt_cursor_ < 0 || tgt_cursor_ >= (int)tgt_list_.size()) return;
        auto& links = tgt_list_[tgt_cursor_].link_targets;
        const std::string& name = tgt_list_[e.idx].name;
        auto it = std::find(links.begin(), links.end(), name);
        if (it != links.end()) links.erase(it);
        else                   links.push_back(name);
    } else if (e.kind == LibEntry::Kind::SYS_LIB) {
        if (e.idx >= 0 && e.idx < (int)m_sys_lib_used_.size())
            m_sys_lib_used_[e.idx] = !m_sys_lib_used_[e.idx];
        rebuildLibEntries(); // re-sort used/available sections
    }
}

// ── onInit ────────────────────────────────────────────────────────────────────

void ProjectPropertiesDialog::onInit()
{
    // Group 0: Info (read-only; draw manually)
    {
        FocusGroup g;
        g.title                = " Project Info ";
        g.box_x                = 2; g.box_y = INFO_BOX_Y;
        g.box_w                = W - 4 - (LIB_BOX_W + 2); g.box_h = INFO_BOX_H;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    // Group 1: Configuration
    {
        FocusGroup g;
        g.title                = " Configuration ";
        g.box_x                = 2; g.box_y = CFG_BOX_Y;
        g.box_w                = W - 4 - (LIB_BOX_W + 2); g.box_h = CFG_BOX_H;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    // Group 2: Targets
    {
        FocusGroup g;
        g.title                = " Targets ";
        g.box_x                = 2; g.box_y = TGT_BOX_Y;
        g.box_w                = W - 4 - (LIB_BOX_W + 2); g.box_h = TGT_BOX_H;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    // Group 3: Libraries
    {
        FocusGroup g;
        g.title                = " Libraries ";
        g.box_x                = LIB_BOX_X; g.box_y = LIB_BOX_Y;
        g.box_w                = LIB_BOX_W; g.box_h = LIB_BOX_H;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    // Button row
    addButtons(ButtonRow{
        .buttons = {
            Button{
                .label = " &New Target ",
                .x = BTN_NEW_TGT_X, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    ProjectTarget t;
                    if (TargetDialog::show(renderer_, t, true)) {
                        tgt_list_.push_back(std::move(t));
                        tgt_cursor_ = (int)tgt_list_.size() - 1;
                        if (tgt_cursor_ >= tgt_scroll_ + TGT_VISIBLE)
                            tgt_scroll_ = tgt_cursor_ - TGT_VISIBLE + 1;
                        setGroupFocus(GRP_TGT);
                    }
                    return HandleResult::CONTINUE;
                }
            },
            Button{
                .label = " &Ok ",
                .x = BTN_OK_X, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
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
            }
        }
    });

    setGroupFocus(GRP_TGT);
    setGroupBtnFocus(1);   // Ok is the default focused button
}

// ── onDraw ────────────────────────────────────────────────────────────────────

void ProjectPropertiesDialog::onDraw(Renderer& renderer, int startx, int starty)
{
    const int inner_x = startx + 2;

    // ── Info box (read-only) ──────────────────────────────────────────────────
    {
        const int fy1 = starty + INFO_BOX_Y + 1;
        renderer.drawText(inner_x + 2, fy1,
                          "Name: " + project_.name, Renderer::CP_DIALOG);
        const int fy2 = fy1 + 1;
        std::string root_disp = project_.root;
        const int max_root = W - 4 - (LIB_BOX_W + 2) - 8;
        if ((int)root_disp.size() > max_root)
            root_disp = "..." + root_disp.substr(root_disp.size() - max_root + 3);
        renderer.drawText(inner_x + 2, fy2,
                          "Root:  " + root_disp, Renderer::CP_DIALOG);
    }

    // ── Configuration ─────────────────────────────────────────────────────────
    {
        const int fy = starty + CFG_BOX_Y + 1;
        bool grp_focused = (getFocusedGroup() == GRP_CFG);
        int  inner_focus = groups()[GRP_CFG].inner_focus;

        renderer.drawText(inner_x + 2, fy, "Build system:", Renderer::CP_DIALOG);
        const char* bs_labels[] = {"CMake", "Make", "Meson"};
        int rx = inner_x + 16;
        for (int i = 0; i < 3; ++i) {
            bool is_selected = (bs_cursor_ == i);
            bool is_cursor   = grp_focused && (inner_focus == 0) && is_selected;
            std::string mark = is_selected ? "(•)" : "( )";
            renderer.drawText(rx, fy, mark + " " + bs_labels[i],
                              is_cursor ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
            rx += 4 + (int)strlen(bs_labels[i]) + 1;
        }

        const int fy2 = starty + CFG_BOX_Y + 2;
        renderer.drawText(inner_x + 2, fy2, "C++ Standard:", Renderer::CP_DIALOG);
        cfg_combo_.draw(renderer, startx, starty, grp_focused && (inner_focus == 1));
    }

    // ── Targets box ───────────────────────────────────────────────────────────
    {
        const int box_inner_x = inner_x + 1;
        const int box_top_y   = starty + TGT_BOX_Y + 1;
        bool grp_focused = (getFocusedGroup() == GRP_TGT);
        const int box_w_inner = W - 4 - (LIB_BOX_W + 2) - 4; // usable width inside box

        if (tgt_list_.empty()) {
            renderer.drawText(box_inner_x, box_top_y, "No targets  (Ins to add)",
                              Renderer::CP_DIALOG);
        } else {
            for (int row = 0; row < TGT_VISIBLE; ++row) {
                int idx = tgt_scroll_ + row;
                if (idx >= (int)tgt_list_.size()) break;
                const auto& tgt = tgt_list_[idx];
                std::string abbr = (tgt.type == "executable")    ? "exe" :
                                   (tgt.type == "static_library") ? "lib" : "dll";
                std::string text = "[" + abbr + "] " + tgt.name;
                if ((int)text.size() < box_w_inner)
                    text += std::string(box_w_inner - (int)text.size(), ' ');
                else
                    text = text.substr(0, box_w_inner);

                bool is_cursor = grp_focused && (idx == tgt_cursor_);
                renderer.drawText(box_inner_x, box_top_y + row, text,
                    is_cursor ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG,
                    is_cursor ? A_BOLD : A_NORMAL);
            }
        }

        // Footer hint
        const int footer_y = starty + TGT_BOX_Y + TGT_BOX_H - 1;
        renderer.drawText(inner_x + 2, footer_y,
                          "Enter=Properties  Del=Remove  Space=CycleType  Ins=Add",
                          Renderer::CP_STATUS_BAR);
    }

    // ── Library list ──────────────────────────────────────────────────────────
    {
        const int lib_x  = startx + LIB_BOX_X + 1;
        const int lib_y0 = starty + LIB_BOX_Y + 1;
        bool grp_focused = (getFocusedGroup() == GRP_LIB);

        // Filter line
        {
            std::string fline = "/ " + m_lib_filter_;
            while ((int)fline.size() < LIB_ITEM_W) fline += ' ';
            fline = fline.substr(0, LIB_ITEM_W);
            renderer.drawText(lib_x, lib_y0, fline,
                              grp_focused ? Renderer::CP_LIST_BOX : Renderer::CP_DIALOG);
        }

        if (m_lib_entries_.empty()) {
            renderer.drawText(lib_x, lib_y0 + 1,
                              m_lib_filter_.empty() ? "No libraries found" : "No matches",
                              Renderer::CP_DIALOG);
        } else {
            for (int row = 0; row < LIB_VISIBLE; ++row) {
                int ei = m_lib_scroll_ + row;
                if (ei >= (int)m_lib_entries_.size()) break;
                const auto& e = m_lib_entries_[ei];

                std::string line;
                int color = Renderer::CP_DIALOG;
                int attr  = A_NORMAL;

                if (e.kind == LibEntry::Kind::HEADER) {
                    line  = e.label;
                    while ((int)line.size() < LIB_ITEM_W) line += ' ';
                    line  = line.substr(0, LIB_ITEM_W);
                    attr  = A_BOLD;
                } else {
                    bool sel = libEntrySelected(e);
                    // PROJ_LIB gets a different marker to distinguish from system libs
                    std::string mark = (e.kind == LibEntry::Kind::PROJ_LIB)
                                        ? (sel ? "[>]" : "[ ]")
                                        : (sel ? "[X]" : "[ ]");
                    std::string text = e.label;
                    const int max_text = LIB_ITEM_W - 4;
                    if ((int)text.size() > max_text) text = text.substr(0, max_text);
                    line = mark + " " + text;
                    while ((int)line.size() < LIB_ITEM_W) line += ' ';

                    bool is_cursor = grp_focused && (ei == m_lib_cursor_);
                    color = is_cursor ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG;
                }

                renderer.drawText(lib_x, lib_y0 + 1 + row, line, color, attr);
            }
        }
    }
}

// ── onKey ─────────────────────────────────────────────────────────────────────

HandleResult ProjectPropertiesDialog::onKey(wint_t ch)
{
    int grp = getFocusedGroup();

    // ── Info group: no interaction ────────────────────────────────────────────
    if (grp == GRP_INFO) {
        return HandleResult::CONTINUE;
    }

    // ── Configuration ─────────────────────────────────────────────────────────
    if (grp == GRP_CFG) {
        auto& g = groups()[GRP_CFG];
        if (ch == KEY_UP || ch == KEY_DOWN) {
            if (ch == KEY_DOWN) g.inner_focus = (g.inner_focus + 1) % 2;
            else                g.inner_focus = (g.inner_focus == 0) ? 1 : 0;
            return HandleResult::CONTINUE;
        }
        if (g.inner_focus == 0) {
            if (ch == KEY_LEFT  && bs_cursor_ > 0) { --bs_cursor_; return HandleResult::CONTINUE; }
            if (ch == KEY_RIGHT && bs_cursor_ < 2) { ++bs_cursor_; return HandleResult::CONTINUE; }
        } else {
            cfg_combo_.handleKey(ch);
        }
        return HandleResult::CONTINUE;
    }

    // ── Targets ───────────────────────────────────────────────────────────────
    if (grp == GRP_TGT) {
        const int count   = (int)tgt_list_.size();
        const int old_cur = tgt_cursor_;
        if (ch == KEY_UP) {
            if (tgt_cursor_ > 0) {
                --tgt_cursor_;
                if (tgt_cursor_ < tgt_scroll_) tgt_scroll_ = tgt_cursor_;
            }
        } else if (ch == KEY_DOWN) {
            if (tgt_cursor_ < count - 1) {
                ++tgt_cursor_;
                if (tgt_cursor_ >= tgt_scroll_ + TGT_VISIBLE)
                    tgt_scroll_ = tgt_cursor_ - TGT_VISIBLE + 1;
            }
        } else if (ch == KEY_IC) {  // Ins — add new target
            ProjectTarget t;
            if (TargetDialog::show(renderer_, t, true)) {
                tgt_list_.push_back(std::move(t));
                tgt_cursor_ = (int)tgt_list_.size() - 1;
                if (tgt_cursor_ >= tgt_scroll_ + TGT_VISIBLE)
                    tgt_scroll_ = tgt_cursor_ - TGT_VISIBLE + 1;
                rebuildLibEntries();
            }
        } else if ((ch == KEY_ENTER || ch == 10 || ch == 13) &&
                   tgt_cursor_ >= 0 && tgt_cursor_ < count) {  // Enter — edit target
            TargetDialog::show(renderer_, tgt_list_[tgt_cursor_], false);
            rebuildLibEntries();
        } else if (ch == KEY_DC) {  // Del — remove target
            if (tgt_cursor_ >= 0 && tgt_cursor_ < count) {
                tgt_list_.erase(tgt_list_.begin() + tgt_cursor_);
                if (tgt_cursor_ >= (int)tgt_list_.size() && tgt_cursor_ > 0)
                    --tgt_cursor_;
                if (tgt_scroll_ > tgt_cursor_)
                    tgt_scroll_ = tgt_cursor_;
                rebuildLibEntries();
            }
        } else if (ch == ' ') {
            // Cycle type: executable -> static_library -> shared_library -> executable
            if (tgt_cursor_ >= 0 && tgt_cursor_ < count) {
                auto& tgt = tgt_list_[tgt_cursor_];
                if      (tgt.type == "executable")     tgt.type = "static_library";
                else if (tgt.type == "static_library") tgt.type = "shared_library";
                else                                   tgt.type = "executable";
                rebuildLibEntries();
            }
        }
        // Rebuild lib entries whenever the focused target changes (proj-lib section filters self)
        if (tgt_cursor_ != old_cur)
            rebuildLibEntries();
        return HandleResult::CONTINUE;
    }

    // ── Library list ──────────────────────────────────────────────────────────
    if (grp == GRP_LIB) {
        const int n = (int)m_lib_entries_.size();

        auto move_up = [&]() {
            int p = m_lib_cursor_ - 1;
            while (p >= 0 && m_lib_entries_[p].kind == LibEntry::Kind::HEADER) --p;
            if (p >= 0) {
                m_lib_cursor_ = p;
                if (m_lib_cursor_ < m_lib_scroll_) m_lib_scroll_ = m_lib_cursor_;
            }
        };
        auto move_down = [&]() {
            int p = m_lib_cursor_ + 1;
            while (p < n && m_lib_entries_[p].kind == LibEntry::Kind::HEADER) ++p;
            if (p < n) {
                m_lib_cursor_ = p;
                if (m_lib_cursor_ >= m_lib_scroll_ + LIB_VISIBLE)
                    m_lib_scroll_ = m_lib_cursor_ - LIB_VISIBLE + 1;
            }
        };

        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!m_lib_filter_.empty()) {
                m_lib_filter_.pop_back();
                rebuildLibEntries();
            }
        } else if (ch == KEY_UP) {
            move_up();
        } else if (ch == KEY_DOWN) {
            move_down();
        } else if (ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13) {
            if (m_lib_cursor_ >= 0 && m_lib_cursor_ < n) {
                const auto& e = m_lib_entries_[m_lib_cursor_];
                if (e.kind != LibEntry::Kind::HEADER)
                    libEntryToggle(e);
            }
        } else if (ch == KEY_PPAGE) {
            for (int i = 0; i < LIB_VISIBLE; ++i) move_up();
        } else if (ch == KEY_NPAGE) {
            for (int i = 0; i < LIB_VISIBLE; ++i) move_down();
        } else if (ch > 31 && ch < KEY_MIN) {
            m_lib_filter_ += static_cast<char>(ch);
            rebuildLibEntries();
        }
        return HandleResult::CONTINUE;
    }

    return HandleResult::CONTINUE;
}

// ── onPlaceCursor ─────────────────────────────────────────────────────────────

bool ProjectPropertiesDialog::onPlaceCursor(Renderer& renderer, int sx, int sy)
{
    int grp = getFocusedGroup();

    if (grp == GRP_LIB) {
        renderer.showCursor();
        int cx = sx + LIB_BOX_X + 1 + 2 + (int)m_lib_filter_.size();
        int max_cx = sx + LIB_BOX_X + LIB_ITEM_W;
        move(sy + LIB_BOX_Y + 1, std::min(cx, max_cx));
        return true;
    }

    return false;
}

// ── onTab ─────────────────────────────────────────────────────────────────────

bool ProjectPropertiesDialog::onTab(bool /*forward*/)
{
    // Let the default DialogBase Tab cycle handle navigation between groups
    return false;
}
