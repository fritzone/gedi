#pragma once
#include "DialogBase.h"
#include "Renderer.h"
#include <string>

// ═══════════════════════════════════════════════════════════════════════════════
// QuestionDialog
//
// Shows a modal yes/no question dialog.
//
// Returns:
//    1  — Yes
//    0  — No
//   -1  — ESC / dismissed without a choice
//
// Keys accepted:
//   Left / Right / Tab / Shift-Tab  — toggle between Yes and No
//   Enter / Space                   — activate focused button
//   Y / y   (or Alt+Y)              — activate Yes directly
//   N / n   (or Alt+N)              — activate No directly
//   ESC                             — dismiss, return -1
// ═══════════════════════════════════════════════════════════════════════════════

class QuestionDialog : private DialogBase {
public:
    static int ask(Renderer&          renderer,
                   const std::string& question,
                   const std::string& info = "");

private:
    QuestionDialog(const std::string& question,
                   const std::string& info);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;

    // Y/N direct keypresses (no Alt needed) — reach here via dispatchChar
    // fall-through when no input field is focused.
    HandleResult onKey(wint_t ch) override;

    // ── Per-instance state ────────────────────────────────────────────────────
    std::string question_;
    std::string info_;      // already truncated to fit the dialog width
};
