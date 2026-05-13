#include "GoToLineDialog.h"
#include <string>

GoToLineDialog::GoToLineDialog(int current_line, int max_lines)
    : DialogBase("Go to Line", /*w=*/50, /*h=*/10)
    , max_lines_(max_lines)
    , line_buf_(std::to_string(current_line))
{}

int GoToLineDialog::show(Renderer& renderer, int current_line, int max_lines)
{
    GoToLineDialog dlg(current_line, max_lines);
    DialogResult r = dlg.run(renderer);
    if (r.cancelled()) return -1;
    return r.as_int("line").value_or(-1);
}

void GoToLineDialog::onInit()
{
    setFocusCount(static_cast<int>(Focus::_count));
    setFocus(static_cast<int>(Focus::INPUTFIELD));
    setButtonRowFocusIndex(static_cast<int>(Focus::BTN_ROW));

    // ── Input field ───────────────────────────────────────────────────────────
    addInput({
        .focus_index  = static_cast<int>(Focus::INPUTFIELD),
        .field_x = 3, .field_y = 4, .field_w = 44,
        .label   = "",   // drawn dynamically in onDraw
        .label_x = 0, .label_y = 0,
        .buffer  = line_buf_,
        .numeric_only = true,
    });

    // ── Button row ────────────────────────────────────────────────────────────
    addButtons(ButtonRow{
        .buttons = {
            Button{
                .label = " &Go ",
                .x = 13, .y = 7,
                .on_activate = [this]() -> HandleResult {
                    int n = -1;
                    try { n = std::stoi(line_buf_); } catch (...) {}
                    if (n < 1)          n = 1;
                    if (n > max_lines_) n = max_lines_;
                    result().accept();
                    result().set("line", std::to_string(n));
                    return HandleResult::CLOSE;
                }
            },
            Button{
                .label = " &Cancel ",
                .x = 27, .y = 7,
                .on_activate = [this]() -> HandleResult {
                    result().cancel();
                    return HandleResult::CLOSE;
                }
            },
        }
    });

    // ── Arrow-key navigation ──────────────────────────────────────────────────
    nav_.link(Direction::DOWN,  Focus::INPUTFIELD, Focus::BTN_ROW)
        .link(Direction::UP,    Focus::BTN_ROW,    Focus::INPUTFIELD);
    setNavigation(nav_);
}

void GoToLineDialog::onDraw(Renderer& renderer, int startx, int starty)
{
    renderer.drawText(startx + 3, starty + 2,
                      "Line Number (1-" + std::to_string(max_lines_) + "):",
                      Renderer::CP_DIALOG);
}
