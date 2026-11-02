#pragma once
#include "DialogBase.h"
#include "CompilerDefs.h"
#include "CompilerSettings.h"
#include "GediProject.h"
#include "Renderer.h"
#include <set>
#include <string>
#include <vector>

class BuildSystem;

// ═══════════════════════════════════════════════════════════════════════════════
// CompileOptionsDialog / Build Options dialog
//
// Per-file mode  (project == nullptr):
//   Title: "Compile Options - <compiler>"
//   "Final Command" preview: the compiler invocation for the current file.
//   Ok writes back to the passed CompilerSettings (EditorBuffer's).
//
// Project mode   (project != nullptr):
//   Title: "Build Options - <project name>"
//   "Build Command" preview: cmake / make / meson invocation with these flags.
//   Ok writes back to the passed CompilerSettings (GediProject's) then the
//   caller is responsible for saving the project.
//
// Everything compiler-specific comes from the configured compiler's definition
// (CompilerDefs.h): the tabs and their options, the top row (the C++ standard,
// or the definition's own top option such as Open Watcom's Target CPU) and the
// command the preview shows. Which compiler that is is chosen in Options >
// Compiler Options (CompilerDialog). Test tries each option on the compiler and
// marks the ones it rejects.
// ═══════════════════════════════════════════════════════════════════════════════

class CompileOptionsDialog : private DialogBase {
public:
    // Per-file mode:  pass project=nullptr and a source filename for the preview.
    // Project mode:   pass the project pointer; filename is ignored.
    static void show(Renderer&          renderer,
                     BuildSystem&       buildSystem,
                     CompilerSettings&  settings,
                     const GediProject* project  = nullptr,
                     const std::string& filename = "");

private:
    CompileOptionsDialog(Renderer&          renderer,
                         BuildSystem&       buildSystem,
                         CompilerSettings&  settings,
                         const GediProject* project,
                         const std::string& filename);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;
    HandleResult onKey(wint_t ch) override;
    bool onTab(bool forward) override;

    //  Layout constants
    // Sized to fit an 80x25 screen. Rows, top to bottom: title border, blank,
    // top row (C++ standard / the compiler's top option), tab bar + its
    // separator, option list, Optional flags box, command box, buttons, the
    // buttons' drop shadow, bottom border. The shadow needs a row of its own or
    // it lands on the border.
    static constexpr int W            = 78;
    static constexpr int H            = 23;
    static constexpr int COMBO_Y      = 2;
    static constexpr int TAB_Y        = 3;       // separator line on TAB_Y + 1
    static constexpr int OPT_Y        = TAB_Y + 2;            // = 5
    static constexpr int BTN_Y        = H - 3;                // = 20 (shadow on 21)
    static constexpr int CMD_BOX_H    = 4;
    static constexpr int CMD_VIEW_H   = CMD_BOX_H - 2;
    static constexpr int CMD_BOX_Y    = BTN_Y - CMD_BOX_H;    // = 16, rows 16..19
    static constexpr int FLAGS_BOX_H  = 3;
    static constexpr int FLAGS_BOX_Y  = CMD_BOX_Y - FLAGS_BOX_H;  // = 13, rows 13..15
    static constexpr int OPT_ROWS     = FLAGS_BOX_Y - OPT_Y;  // = 8 visible rows

    std::string previewCommand();       // what the preview box and Copy show
    std::vector<OptionList::Option> options(int tab);   // the list for one tab
    void runTest();                     // Test: try every option on the compiler
    std::vector<int> tabStops();        // focus order

    Renderer&                    renderer_;
    BuildSystem&                 buildSystem_;
    const compilers::Definition& def_;       // the configured compiler
    CompilerSettings&            settings_;  // target written on Ok
    const GediProject*           project_;   // null = per-file mode
    std::string                  filename_;  // source file for per-file preview

    CompilerSettings      temp_;             // working copy
    CompilerOptionValues* vals_ = nullptr;   // temp_'s values for def_

    std::vector<std::string> top_items_;     // the top row's combo items
    std::set<std::string>    unsupported_;   // options the compiler rejected when tested
    int  cmd_top_row_ = 0;

    static constexpr int GRP_STANDARD = 0;   // the top row
    static constexpr int GRP_TABS     = 1;
    static constexpr int GRP_OPTIONS  = 2;
    static constexpr int GRP_FLAGS    = 3;
    static constexpr int GRP_CMD      = 4;

    static constexpr int BTN_COUNT    = 4;   // Ok / Test / Copy / Cancel
};
