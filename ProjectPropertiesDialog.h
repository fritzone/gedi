#pragma once

#include "DialogBase.h"
#include "GediProject.h"
#include "LibraryInfo.h"
#include "TargetDialog.h"
#include "Widgets.h"
#include <string>
#include <vector>

class ProjectPropertiesDialog : private DialogBase {
public:
    static bool show(Renderer& renderer, GediProject& project,
                     const std::vector<LibraryInfo>& all_libs);

private:
    ProjectPropertiesDialog(Renderer& renderer, GediProject& project,
                            const std::vector<LibraryInfo>& all_libs);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;
    HandleResult onKey(wint_t ch) override;
    bool onPlaceCursor(Renderer& renderer, int sx, int sy) override;
    bool onTab(bool forward) override;

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int W           = 100;
    static constexpr int H           = 30;

    // Left panel
    static constexpr int INFO_BOX_Y  = 1;
    static constexpr int INFO_BOX_H  = 3;
    static constexpr int CFG_BOX_Y   = 4;
    static constexpr int CFG_BOX_H   = 5;
    static constexpr int TGT_BOX_Y   = 9;
    static constexpr int TGT_BOX_H   = 18;
    static constexpr int TGT_VISIBLE = TGT_BOX_H - 2;  // 16 visible rows

    // Right panel (library list)
    static constexpr int LIB_BOX_X   = 65;
    static constexpr int LIB_BOX_W   = 32;
    static constexpr int LIB_BOX_Y   = 1;
    static constexpr int LIB_BOX_H   = H - 4;   // 26
    static constexpr int LIB_VISIBLE  = LIB_BOX_H - 3;  // 23 visible rows
    static constexpr int LIB_ITEM_W   = LIB_BOX_W - 2;  // 30

    // Buttons
    static constexpr int BTN_Y           = H - 3;
    static constexpr int BTN_NEW_TGT_X   = 4;
    static constexpr int BTN_OK_X        = 42;
    static constexpr int BTN_CANCEL_X    = BTN_OK_X + 9;

    // Group indices
    static constexpr int GRP_INFO = 0;
    static constexpr int GRP_CFG  = 1;
    static constexpr int GRP_TGT  = 2;
    static constexpr int GRP_LIB  = 3;

    Renderer&   renderer_;
    GediProject& project_;

    // Config state
    int  bs_cursor_ = 0;   // build-system radio (0=cmake,1=make,2=meson)
    ComboBox cfg_combo_;
    std::vector<std::string> standards_;
    int std_idx_ = 0;

    // Targets state
    int tgt_cursor_ = 0;
    int tgt_scroll_ = 0;
    std::vector<ProjectTarget> tgt_list_; // editable copy

    // ── Library panel ─────────────────────────────────────────────────────────
    // A flat list combining project-library targets and system libraries,
    // rebuilt whenever the filter or target selection changes.
    struct LibEntry {
        enum class Kind { HEADER, PROJ_LIB, SYS_LIB } kind;
        std::string label;  // display text
        int         idx;    // PROJ_LIB: into tgt_list_; SYS_LIB: into m_sys_libs_; HEADER: -1
    };

    std::vector<LibraryInfo> m_sys_libs_;      // all system libs from all_libs
    std::vector<bool>        m_sys_lib_used_;  // which system libs are in the project
    std::string              m_lib_filter_;
    std::vector<LibEntry>    m_lib_entries_;   // visible entries after filtering
    int                      m_lib_cursor_ = 0;
    int                      m_lib_scroll_  = 0;

    void rebuildLibEntries();
    bool libEntrySelected(const LibEntry& e) const;
    void libEntryToggle(const LibEntry& e);
};
