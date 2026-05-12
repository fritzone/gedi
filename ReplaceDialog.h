#pragma once
#include "DialogBase.h"
#include "Renderer.h"
#include <string>

// ═══════════════════════════════════════════════════════════════════════════════
// ReplaceDialog
//
// Shows a modal Find & Replace dialog.
//
// Returned DialogResult fields:
//   result["action"]  → "replace" | "replace_all" | "" (cancel)
//   result["find"]    → the find text
//   result["replace"] → the replacement text
// ═══════════════════════════════════════════════════════════════════════════════

class ReplaceDialog : private DialogBase {
public:
    static DialogResult show(Renderer& renderer,
                             const std::string& initial_find    = "",
                             const std::string& initial_replace = "");

private:
    ReplaceDialog(const std::string& initial_find,
                  const std::string& initial_replace);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;

    // ── Focus enum ────────────────────────────────────────────────────────────
    DeclareCyclicEnum(Focus, FIND, REPLACE, BTN_REPLACE, BTN_REPLACE_ALL, BTN_CANCEL);

    // ── Per-instance state ────────────────────────────────────────────────────
    std::string find_buf_;
    std::string replace_buf_;

    NavigationGraph<Focus> nav_;
};
