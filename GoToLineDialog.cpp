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

    // Input field
    //  Layout (w=50):
    //    row 2: label "Line Number (1-N):"  at x=3
    //    row 4: input field                 at x=3, w=44
    addInput({
        .focus_index = static_cast<int>(Focus::INPUTFIELD),
        .field_x = 3, .field_y = 4, .field_w = 44,
        .label   = "",   // drawn dynamically in onDraw (needs max_lines_)
        .label_x = 0, .label_y = 0,
        .buffer  = line_buf_,
        .numeric_only = true,
    });

    // Buttons
    //  Centred at row h-3 = 7
    addButton({
        .focus_index = static_cast<int>(Focus::BTN_GO),
        .btn_x = 13, .btn_y = 7,
        .label = " &Go ",
        .on_activate = [this]() -> HandleResult {
            int n = -1;
            try { n = std::stoi(line_buf_); } catch (...) {}
            if (n < 1)          n = 1;
            if (n > max_lines_) n = max_lines_;
            result().accept();
            result().set("line", std::to_string(n));
            return HandleResult::CLOSE;
        }
    });

    addButton({
        .focus_index = static_cast<int>(Focus::BTN_CANCEL),
        .btn_x = 27, .btn_y = 7,
        .label = " &Cancel ",
        .on_activate = [this]() -> HandleResult {
            result().cancel();
            return HandleResult::CLOSE;
        }
    });

    // Arrow-key navigation setup
    nav_.link(Direction::DOWN,  Focus::INPUTFIELD, Focus::BTN_GO)
        .link(Direction::UP,    Focus::BTN_GO,     Focus::INPUTFIELD)
        .link(Direction::UP,    Focus::BTN_CANCEL,  Focus::INPUTFIELD)
        .link(Direction::RIGHT, Focus::BTN_GO,      Focus::BTN_CANCEL)
        .link(Direction::LEFT,  Focus::BTN_CANCEL,  Focus::BTN_GO);

    setNavigation(nav_);
}

void GoToLineDialog::onDraw(Renderer& renderer, int startx, int starty)
{
    renderer.drawText(startx + 3, starty + 2,
        "Line Number (1-" + std::to_string(max_lines_) + "):",
        Renderer::CP_DIALOG);
}
