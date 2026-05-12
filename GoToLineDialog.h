#pragma once
#include "DialogBase.h"
#include "Renderer.h"

// ═══════════════════════════════════════════════════════════════════════════════
// GoToLineDialog
//
// Shows a modal "Go to Line" dialog.
// Returns the chosen line number (1-based), or -1 if cancelled.
// ═══════════════════════════════════════════════════════════════════════════════

class GoToLineDialog : private DialogBase {
public:
    static int show(Renderer& renderer,
                    int current_line,
                    int max_lines);

private:
    GoToLineDialog(int current_line, int max_lines);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;

    // ── Focus enum ────────────────────────────────────────────────────────────
    DeclareCyclicEnum(Focus, INPUTFIELD, BTN_GO, BTN_CANCEL);

    // ── Per-instance state ────────────────────────────────────────────────────
    int         max_lines_;
    std::string line_buf_;

    NavigationGraph<Focus> nav_;
};
