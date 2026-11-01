#pragma once
#include "Config.h"
#include "DialogBase.h"
#include "CompilerSettings.h"
#include "GediProject.h"
#include "Renderer.h"
#include "ToolchainDetect.h"
#include <string>
#include <vector>

class BuildSystem;

// ═══════════════════════════════════════════════════════════════════════════════
// CompileOptionsDialog / Build Options dialog
//
// Per-file mode  (project == nullptr):
//   Title: "Compiler Options"
//   "Final Command" preview: gcc / clang invocation for the current file.
//   Ok writes back to the passed CompilerSettings (EditorBuffer's).
//
// Project mode   (project != nullptr):
//   Title: "Build Options — <project name>"
//   "Build Command" preview: cmake / make / meson invocation with these flags.
//   Ok writes back to the passed CompilerSettings (GediProject's) then the
//   caller is responsible for saving the project.
//
// The "Compiler" tab is the same in both modes: it names the compiler currently
// in use and lets it be changed, the way setup does. That setting is global -
// it lives in toolchain.json, not in the per-file or per-project options - so Ok
// writes it there, applies its environment to the running editor and tells
// BuildSystem to forget what it derived from the old one.
// ═══════════════════════════════════════════════════════════════════════════════

class CompileOptionsDialog : private DialogBase {
public:
    // Per-file mode:  pass project=nullptr and a source filename for the preview.
    // Project mode:   pass the project pointer; filename is ignored.
    // `config` is the editor's live configuration; its toolchain is updated in
    // place when the user picks a different compiler.
    static void show(Renderer&          renderer,
                     BuildSystem&       buildSystem,
                     Config&            config,
                     CompilerSettings&  settings,
                     const GediProject* project  = nullptr,
                     const std::string& filename = "");

private:
    CompileOptionsDialog(Renderer&          renderer,
                         BuildSystem&       buildSystem,
                         Config&            config,
                         CompilerSettings&  settings,
                         const GediProject* project,
                         const std::string& filename);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;
    HandleResult onKey(wint_t ch) override;
    bool onTab(bool forward) override;
    bool onMouseClick(const MEVENT& ev, int startx, int starty) override;

    //  Layout constants 
    // Sized to fit an 80x25 screen (was 84x34). Vertical layout is packed so the
    // scrollable option list keeps a usable number of rows at this height.
    static constexpr int W            = 78;
    static constexpr int H            = 23;
    static constexpr int COMBO_Y      = 2;
    static constexpr int TAB_Y        = 4;
    static constexpr int OPT_Y        = 5;
    static constexpr int FLAGS_BOX_Y  = H - 9;   // = 14
    static constexpr int FLAGS_BOX_H  = 3;       // rows 14..16
    static constexpr int OPT_ROWS     = FLAGS_BOX_Y - OPT_Y;  // = 9 visible rows
    static constexpr int OPT_BOX_H   = OPT_ROWS + 2;
    static constexpr int CMD_BOX_Y    = H - 6;   // = 17
    static constexpr int CMD_BOX_H    = 4;       // rows 17..20
    static constexpr int CMD_VIEW_H   = 2;
    static constexpr int BTN_Y        = H - 2;   // = 21

    // Compiler tab: fills the same rows the option list uses (OPT_Y .. OPT_Y+8).
    static constexpr int TC_CUR_Y    = OPT_Y;         // "Current:" line
    static constexpr int TC_CURP_Y   = OPT_Y + 1;     // the path it resolves to
    static constexpr int TC_KIND_Y   = OPT_Y + 3;     // first of four radio rows
    static constexpr int TC_PATH_Y   = OPT_Y + 7;     // path field + Browse
    static constexpr int TC_NOTE_Y   = OPT_Y + 8;     // what the pick resolved to
    static constexpr int TC_FIELD_X  = 10;
    static constexpr int TC_BROWSE_X = W - 14;
    static constexpr int TC_BROWSE_W = 9;             // " &Browse "
    static constexpr int TC_FIELD_W  = TC_BROWSE_X - TC_FIELD_X - 2;

    //  Compiler tab
    bool compilerTab();                 // is the Compiler tab the active one?
    void drawCompilerTab(Renderer& renderer, int sx, int sy);
    void probeCompiler();               // refresh pending_ / tc_error_ for the current pick
    void applyCompiler();               // record the pick and make the editor use it
    void browseForCompiler();           // the Open File dialog, filtered to this kind
    void pressBrowse();                 // press animation, then browseForCompiler()
    bool tcPathUsable() const;          // false for MSVC, which locates itself
    std::string previewCommand();       // what the preview box and Copy show
    std::vector<int> tabStops();        // focus order, minus the hidden group

    Renderer&          renderer_;
    BuildSystem&       buildSystem_;
    Config&            config_;         // the editor's live configuration
    CompilerSettings&  settings_;       // target written on Ok
    const GediProject* project_;        // null = per-file mode
    std::string        filename_;       // source file for per-file preview

    CompilerSettings   temp_;           // working copy

    std::vector<std::string> standards_;
    int  std_idx_  = 0;
    int  cmd_top_row_ = 0;

    //  Compiler tab state
    std::vector<toolchain::Kind> tc_kinds_;   // the pickable kinds, in display order
    int  tc_sel_ = 0;                   // index into tc_kinds_
    std::string tc_path_;               // compiler path for the kinds that need one
    int  tc_path_cursor_ = 0;
    int  tc_path_scroll_ = 0;
    bool tc_dirty_   = true;            // the pick changed; re-probe before drawing
    bool tc_changed_ = false;           // the user actually touched it; Ok applies
    toolchain::Setup pending_;          // what the current pick resolves to
    std::string tc_error_;
    std::string current_desc_;          // version banner of the compiler in use

    static constexpr int GRP_STANDARD = 0;
    static constexpr int GRP_TABS     = 1;
    static constexpr int GRP_OPTIONS  = 2;
    static constexpr int GRP_FLAGS    = 3;
    static constexpr int GRP_CMD      = 4;
    static constexpr int GRP_COMPILER = 5;

    // GRP_COMPILER inner-focus items, like NewProjectDialog's path group.
    static constexpr int TC_INNER_KIND   = 0;
    static constexpr int TC_INNER_PATH   = 1;
    static constexpr int TC_INNER_BROWSE = 2;

    static constexpr int TAB_COMPILER = 3;   // index in the tab bar
    static constexpr int BTN_COUNT    = 3;   // Ok / Copy / Cancel
};
