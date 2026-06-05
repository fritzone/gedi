#pragma once
#include "DialogBase.h"
#include "CompilerSettings.h"
#include "GediProject.h"
#include "Renderer.h"
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

    // ── Layout constants ──────────────────────────────────────────────────────
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

    Renderer&          renderer_;
    BuildSystem&       buildSystem_;
    CompilerSettings&  settings_;       // target written on Ok
    const GediProject* project_;        // null = per-file mode
    std::string        filename_;       // source file for per-file preview

    CompilerSettings   temp_;           // working copy

    std::vector<std::string> standards_;
    int  std_idx_  = 0;
    int  cmd_top_row_ = 0;

    static constexpr int GRP_STANDARD = 0;
    static constexpr int GRP_TABS     = 1;
    static constexpr int GRP_OPTIONS  = 2;
    static constexpr int GRP_FLAGS    = 3;
    static constexpr int GRP_CMD      = 4;
};
