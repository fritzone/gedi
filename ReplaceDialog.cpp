#include "ReplaceDialog.h"

ReplaceDialog::ReplaceDialog(const std::string& initial_find,
                             const std::string& initial_replace)
    : DialogBase("Replace", /*w=*/55, /*h=*/10)
    , find_buf_   (initial_find)
    , replace_buf_(initial_replace)
{}

DialogResult ReplaceDialog::show(Renderer& renderer,
                                 const std::string& initial_find,
                                 const std::string& initial_replace)
{
    ReplaceDialog dlg(initial_find, initial_replace);
    return dlg.run(renderer);
}

void ReplaceDialog::onInit()
{
    setFocusCount(static_cast<int>(Focus::_count));
    setFocus(static_cast<int>(Focus::FIND));
    setButtonRowFocusIndex(static_cast<int>(Focus::BTN_ROW));

    // ── Inputs ────────────────────────────────────────────────────────────────
    addInput({
        .focus_index = static_cast<int>(Focus::FIND),
        .field_x = 17, .field_y = 2, .field_w = 35,
        .label   = "Find what:",
        .label_x = 3, .label_y = 2,
        .buffer  = find_buf_,
    });
    addInput({
        .focus_index = static_cast<int>(Focus::REPLACE),
        .field_x = 17, .field_y = 4, .field_w = 35,
        .label   = "Replace with:",
        .label_x = 3, .label_y = 4,
        .buffer  = replace_buf_,
    });

    // ── Button row (all three buttons share one Tab stop) ─────────────────────
    addButtons(ButtonRow{
        .buttons = {
            Button{
                .label = " &Replace ",
                .x = 4, .y = 7,
                .on_activate = [this]() -> HandleResult {
                    result().accept();
                    result().set("action",  "replace");
                    result().set("find",    find_buf_);
                    result().set("replace", replace_buf_);
                    return HandleResult::CLOSE;
                }
            },
            Button{
                .label = " Replace &All ",
                .x = 18, .y = 7,
                .on_activate = [this]() -> HandleResult {
                    result().accept();
                    result().set("action",  "replace_all");
                    result().set("find",    find_buf_);
                    result().set("replace", replace_buf_);
                    return HandleResult::CLOSE;
                }
            },
            Button{
                .label = " &Cancel ",
                .x = 41, .y = 7,
                .on_activate = [this]() -> HandleResult {
                    result().cancel();
                    return HandleResult::CLOSE;
                }
            },
        }
    });

    // ── Arrow-key navigation ──────────────────────────────────────────────────
    nav_.link(Direction::DOWN, Focus::FIND,    Focus::REPLACE)
        .link(Direction::UP,   Focus::REPLACE, Focus::FIND)
        .link(Direction::DOWN, Focus::REPLACE, Focus::BTN_ROW)
        .link(Direction::UP,   Focus::BTN_ROW, Focus::REPLACE);
    setNavigation(nav_);
}

void ReplaceDialog::onDraw(Renderer& /*renderer*/, int /*startx*/, int /*starty*/) {}
