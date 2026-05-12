#include "ReplaceDialog.h"

// ═══════════════════════════════════════════════════════════════════════════════
// ReplaceDialog.cpp
//
// Only what is unique to this dialog lives here.
// Layout (w=55, h=10):
//   row 2: "Find what:"    label  x=3  | input x=17, w=35
//   row 4: "Replace with:" label  x=3  | input x=17, w=35
//   row 7: [ Replace ]  [ Replace All ]  [ Cancel ]
// ═══════════════════════════════════════════════════════════════════════════════

ReplaceDialog::ReplaceDialog(const std::string& initial_find,
                             const std::string& initial_replace)
    : DialogBase("Replace", /*w=*/55, /*h=*/10)
    , find_buf_   (initial_find)
    , replace_buf_(initial_replace)
{}

// ── Static factory ────────────────────────────────────────────────────────────

DialogResult ReplaceDialog::show(Renderer& renderer,
                                 const std::string& initial_find,
                                 const std::string& initial_replace)
{
    ReplaceDialog dlg(initial_find, initial_replace);
    return dlg.run(renderer);
    // Callers inspect result["action"], result["find"], result["replace"]
}

// ── onInit ────────────────────────────────────────────────────────────────────

void ReplaceDialog::onInit()
{
    setFocusCount(static_cast<int>(Focus::_count));
    setFocus(static_cast<int>(Focus::FIND));

    // ── Input: Find ───────────────────────────────────────────────────────────
    addInput({
        .focus_index  = static_cast<int>(Focus::FIND),
        .field_x = 17, .field_y = 2, .field_w = 35,
        .label   = "Find what:",
        .label_x = 3,  .label_y = 2,
        .buffer  = find_buf_,
    });

    // ── Input: Replace ────────────────────────────────────────────────────────
    addInput({
        .focus_index  = static_cast<int>(Focus::REPLACE),
        .field_x = 17, .field_y = 4, .field_w = 35,
        .label   = "Replace with:",
        .label_x = 3,  .label_y = 4,
        .buffer  = replace_buf_,
    });

    // ── Buttons ───────────────────────────────────────────────────────────────
    addButton({
        .focus_index = static_cast<int>(Focus::BTN_REPLACE),
        .btn_x = 4, .btn_y = 7,
        .label = " &Replace ",
        .on_activate = [this]() -> HandleResult {
            result().accept();
            result().set("action",  "replace");
            result().set("find",    find_buf_);
            result().set("replace", replace_buf_);
            return HandleResult::CLOSE;
        }
    });

    addButton({
        .focus_index = static_cast<int>(Focus::BTN_REPLACE_ALL),
        .btn_x = 18, .btn_y = 7,
        .label = " Replace &All ",
        .on_activate = [this]() -> HandleResult {
            result().accept();
            result().set("action",  "replace_all");
            result().set("find",    find_buf_);
            result().set("replace", replace_buf_);
            return HandleResult::CLOSE;
        }
    });

    addButton({
        .focus_index = static_cast<int>(Focus::BTN_CANCEL),
        .btn_x = 41, .btn_y = 7,
        .label = " &Cancel ",
        .on_activate = [this]() -> HandleResult {
            result().cancel();
            return HandleResult::CLOSE;
        }
    });

    // ── Arrow-key navigation ──────────────────────────────────────────────────
    nav_.link(Direction::DOWN,  Focus::FIND,            Focus::REPLACE)
        .link(Direction::UP,    Focus::REPLACE,          Focus::FIND)
        .link(Direction::DOWN,  Focus::REPLACE,          Focus::BTN_REPLACE)
        .link(Direction::UP,    Focus::BTN_REPLACE,      Focus::REPLACE)
        .link(Direction::UP,    Focus::BTN_REPLACE_ALL,  Focus::REPLACE)
        .link(Direction::UP,    Focus::BTN_CANCEL,       Focus::REPLACE)
        .link(Direction::RIGHT, Focus::BTN_REPLACE,      Focus::BTN_REPLACE_ALL)
        .link(Direction::RIGHT, Focus::BTN_REPLACE_ALL,  Focus::BTN_CANCEL)
        .link(Direction::LEFT,  Focus::BTN_REPLACE_ALL,  Focus::BTN_REPLACE)
        .link(Direction::LEFT,  Focus::BTN_CANCEL,       Focus::BTN_REPLACE_ALL);

    setNavigation(nav_);
}

// ── onDraw: no dynamic content beyond what the base draws ────────────────────

void ReplaceDialog::onDraw(Renderer& /*renderer*/, int /*startx*/, int /*starty*/)
{
    // Labels are registered in addInput() and drawn by the base class.
    // Nothing extra is needed here for this dialog.
}
