#ifndef NEWPROJECTDIALOG_H
#define NEWPROJECTDIALOG_H

#include "DialogBase.h"
#include "LibraryInfo.h"
#include "Widgets.h"
#include <string>
#include <vector>

// 
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
    bool onMouseClick(const MEVENT& ev, int startx, int starty) override;
    bool onTab(bool forward) override;

    //  Layout constants
    // Tabbed layout (mirrors the Editor Settings dialog): a tab bar at the top,
    // then a shared content area. Tab 0 "Project" holds Name / Location /
    // Configuration; tab 1 "Libraries" holds the (now full-width) library list.
    // Sized to fit an 80x25 screen.
    static constexpr int W            = 72;
    static constexpr int H            = 22;
    static constexpr int INNER_W      = W - 4;      // = 68
    static constexpr int TAB_Y        = 2;          // tab-label row
    static constexpr int CONTENT_Y    = 4;          // first content row (below tab separator)
    static constexpr int CONTENT_H    = H - 7;      // = 15 content rows (4..18)
    static constexpr int BTN_Y        = H - 3;      // = 19

    static constexpr int FIELD_X      = 9;    // left edge of fields (from inner_x = startx+2)
    static constexpr int FIELD_W_NAME = 48;   // name field width
    static constexpr int FIELD_W_PATH = 38;   // path field width

    // Tab 0 - Project: box positions inside the content area.
    static constexpr int NAME_BOX_Y   = CONTENT_Y;        // = 4
    static constexpr int NAME_BOX_H   = 3;
    static constexpr int PATH_BOX_Y   = CONTENT_Y + 4;    // = 8
    static constexpr int PATH_BOX_H   = 5;   // path/Browse row + shadow row + checkbox row
    static constexpr int CFG_BOX_Y    = CONTENT_Y + 9;    // = 13
    static constexpr int CFG_BOX_H    = 5;   // radios + C++ Standard + checkboxes
    static constexpr int GRP_W        = INNER_W;          // tab-0 boxes span the content width

    // Browse "button" drawn inside the Location box, right of the path field.
    static constexpr int BROWSE_BTN_X = 2 + FIELD_X + FIELD_W_PATH + 1; // = 50
    static constexpr int BROWSE_BTN_Y = PATH_BOX_Y + 1;                 // = 9

    // Tab 1 - Libraries: the list fills the whole content area.
    static constexpr int LIB_BOX_X   = 2;
    static constexpr int LIB_BOX_Y   = CONTENT_Y;        // = 4
    static constexpr int LIB_BOX_W   = INNER_W;          // = 68
    static constexpr int LIB_BOX_H   = CONTENT_H;        // = 15
    static constexpr int LIB_VISIBLE  = LIB_BOX_H - 3;   // = 12 visible rows (1 row for filter)
    static constexpr int LIB_ITEM_W   = LIB_BOX_W - 2;   // = 66 usable chars

    // Button positions (centered)
    static constexpr int BTN_CREATE_X = 25;
    static constexpr int BTN_CANCEL_X = BTN_CREATE_X + 11 + 1;  // = 37

    // Focus groups: tab bar first, then the tab-0 content groups, then tab-1.
    static constexpr int GRP_TABS = 0;
    static constexpr int GRP_NAME = 1;
    static constexpr int GRP_PATH = 2;
    static constexpr int GRP_CFG  = 3;
    static constexpr int GRP_LIB  = 4;

    // GRP_PATH inner-focus items
    static constexpr int PATH_INNER_FIELD  = 0;
    static constexpr int PATH_INNER_BROWSE = 1;
    static constexpr int PATH_INNER_CHECK  = 2;

    // Button indices within the ButtonRow
    static constexpr int BTN_IDX_CREATE = 0;
    static constexpr int BTN_IDX_CANCEL = 1;

    int activeTab();   // convenience accessor for the tab control's active tab

    //  Application references 
    Renderer&        renderer_;
    ProjectTemplate& m_template;

    //  Text-field cursor/scroll state 
    int name_cursor_ = 0;   // byte offset into m_template.name
    int name_scroll_ = 0;   // first visible byte offset
    int path_cursor_ = 0;
    int path_scroll_ = 0;

    //  Config group state (managed manually) 
    int      bs_cursor_;         // build-system radio cursor
    int      cfg_chk_cursor_ = 0; // checkbox row cursor: 0=init_git, 1=create_main
    ComboBox cfg_combo_;
    std::vector<std::string> standards_;
    int                      std_idx_;

    //  Library panel state 
    std::vector<LibraryInfo> m_libraries;
    std::vector<bool>        m_lib_selected;
    std::vector<int>         m_lib_filtered;   // indices into m_libraries that pass the filter
    std::string              m_lib_filter;
    int                      m_lib_cursor = 0;
    int                      m_lib_scroll = 0;

    void rebuildFilter();
    void openBrowse();
};

#endif // NEWPROJECTDIALOG_H
