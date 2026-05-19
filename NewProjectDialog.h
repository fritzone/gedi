#ifndef NEWPROJECTDIALOG_H
#define NEWPROJECTDIALOG_H

#include "DialogBase.h"
#include "LibraryInfo.h"
#include "Widgets.h"
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
struct ProjectTemplate {
    std::string name;
    std::string path;
    int         build_system       = 0;     // 0: CMake, 1: Make, 2: Meson
    std::string cpp_standard       = "c++17";
    bool        init_git           = false;
    bool        create_main        = true;
    bool        create_project_dir = true;
    std::vector<LibraryInfo> selected_libraries;
};

class NewProjectDialog : private DialogBase {
public:
    static bool show(Renderer& renderer, ProjectTemplate& out_template,
                     const std::vector<LibraryInfo>& libs);
    static std::vector<LibraryInfo> loadLibraries();

private:
    NewProjectDialog(Renderer& renderer, ProjectTemplate& t);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;
    HandleResult onKey(wint_t ch) override;
    bool onPlaceCursor(Renderer& renderer, int sx, int sy) override;
    bool onTab(bool forward) override;

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int W            = 100;
    static constexpr int H            = 18;
    static constexpr int FIELD_X      = 16;   // left edge of fields (from inner_x = startx+2)
    static constexpr int FIELD_W_NAME = 42;   // name field width
    static constexpr int FIELD_W_PATH = 34;   // path field width
    static constexpr int NAME_BOX_Y   = 1;
    static constexpr int NAME_BOX_H   = 3;
    static constexpr int PATH_BOX_Y   = 5;
    static constexpr int PATH_BOX_H   = 5;   // Browse row + shadow row + checkbox row
    static constexpr int CFG_BOX_Y    = 10;
    static constexpr int CFG_BOX_H    = 5;   // two content rows: radio + C++ Standard
    static constexpr int BTN_Y        = H - 3;  // = 15

    // Browse button inside the Location box
    static constexpr int BROWSE_BTN_X = 2 + FIELD_X + FIELD_W_PATH + 1; // = 54
    static constexpr int BROWSE_BTN_Y = PATH_BOX_Y + 1;                  // = 6

    // Library panel (right side)
    static constexpr int LIB_BOX_X   = 65;
    static constexpr int LIB_BOX_W   = 32;
    static constexpr int LIB_BOX_Y   = 1;
    static constexpr int LIB_BOX_H   = H - 4;  // = 14
    static constexpr int LIB_VISIBLE  = LIB_BOX_H - 3;   // = 11 visible rows (1 row for filter)
    static constexpr int LIB_ITEM_W   = LIB_BOX_W - 2;   // = 30 usable chars

    // Button positions (centered under left half)
    static constexpr int BTN_CREATE_X = 21;
    static constexpr int BTN_CANCEL_X = BTN_CREATE_X + 11 + 4;  // = 36

    static constexpr int GRP_NAME = 0;
    static constexpr int GRP_PATH = 1;
    static constexpr int GRP_CFG  = 2;
    static constexpr int GRP_LIB  = 3;

    // Button indices within the ButtonRow
    static constexpr int BTN_IDX_CREATE = 0;
    static constexpr int BTN_IDX_CANCEL = 1;
    static constexpr int BTN_IDX_BROWSE = 2;

    // ── Application references ────────────────────────────────────────────────
    Renderer&        renderer_;
    ProjectTemplate& m_template;

    // ── Text-field cursor/scroll state ────────────────────────────────────────
    int name_cursor_ = 0;   // byte offset into m_template.name
    int name_scroll_ = 0;   // first visible byte offset
    int path_cursor_ = 0;
    int path_scroll_ = 0;

    // ── Config group state (managed manually) ─────────────────────────────────
    int      bs_cursor_;         // build-system radio cursor
    int      cfg_chk_cursor_ = 0; // checkbox row cursor: 0=init_git, 1=create_main
    ComboBox cfg_combo_;
    std::vector<std::string> standards_;
    int                      std_idx_;

    // ── Library panel state ───────────────────────────────────────────────────
    std::vector<LibraryInfo> m_libraries;
    std::vector<bool>        m_lib_selected;
    std::vector<int>         m_lib_filtered;   // indices into m_libraries that pass the filter
    std::string              m_lib_filter;
    int                      m_lib_cursor = 0;
    int                      m_lib_scroll = 0;

    void rebuildFilter();
};

#endif // NEWPROJECTDIALOG_H
