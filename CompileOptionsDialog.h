#pragma once
#include "DialogBase.h"
#include "CompilerSettings.h"
#include "Renderer.h"
#include <string>
#include <vector>

// Forward declarations
class BuildSystem;
struct EditorBuffer;

// ═══════════════════════════════════════════════════════════════════════════════
// CompileOptionsDialog
//
// A tabbed compiler-options dialog with:
//   Group 0 — C++ Standard   (ComboBox)
//   Group 1 — Tab bar        (TabControl)
//   Group 2 — Option list    (OptionList — content changes with active tab)
//   Group 3 — Optional flags (raw text input)
//   Group 4 — Final command  (read-only scrollable view, focused = scrollable)
//   ButtonRow — Ok | Copy | Cancel
//
// On Ok:   settings are written back to buffer.compiler_settings.
// On Copy: final command is copied to clipboard; dialog stays open.
// On Cancel / ESC: no changes.
// ═══════════════════════════════════════════════════════════════════════════════

class CompileOptionsDialog : private DialogBase {
public:
    static void show(Renderer&    renderer,
                     BuildSystem& buildSystem,
                     EditorBuffer& buffer);

private:
    CompileOptionsDialog(Renderer&    renderer,
                         BuildSystem& buildSystem,
                         EditorBuffer& buffer);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;
    HandleResult onKey(wint_t ch) override;   // scrolls the command view

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int W            = 84;
    static constexpr int H            = 34;
    static constexpr int COMBO_Y      = 2;
    static constexpr int TAB_Y        = 4;
    static constexpr int OPT_Y        = 6;
    static constexpr int OPT_ROWS     = H - 20;   // visible option rows
    static constexpr int OPT_BOX_H    = OPT_ROWS + 2;
    static constexpr int FLAGS_BOX_Y  = H - 13;
    static constexpr int FLAGS_BOX_H  = 3;
    static constexpr int CMD_BOX_Y    = H - 10;
    static constexpr int CMD_BOX_H    = 4;
    static constexpr int CMD_VIEW_H   = 2;        // visible command lines
    static constexpr int BTN_Y        = H - 4;

    // ── Application references ────────────────────────────────────────────────
    Renderer&     renderer_;
    BuildSystem&  buildSystem_;
    EditorBuffer& buffer_;

    // ── Temporary working copy of settings ───────────────────────────────────
    CompilerSettings temp_;

    // ── Widget state (owned here, referenced by widgets) ─────────────────────
    std::vector<std::string> standards_;
    int  std_idx_  = 0;    // ComboBox selection

    // Optional flags buffer (text_buffer group)
    // (temp_.optional_flags is used directly)

    // Command view scroll
    int cmd_top_row_ = 0;

    // ── Group indices ─────────────────────────────────────────────────────────
    // Kept as named constants so onKey can reference the command-view group.
    static constexpr int GRP_STANDARD = 0;
    static constexpr int GRP_TABS     = 1;
    static constexpr int GRP_OPTIONS  = 2;
    static constexpr int GRP_FLAGS    = 3;
    static constexpr int GRP_CMD      = 4;
};
