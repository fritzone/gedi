#include "ReplaceDialog.h"

ReplaceDialog::ReplaceDialog(const std::string& initial_find,
                             const std::string& initial_replace,
                             ReplaceCallback    on_find_next,
                             ReplaceCallback    on_replace,
                             ReplaceCallback    on_replace_all)
    : DialogBase("Replace", /*w=*/64, /*h=*/11)
    , find_buf_    (initial_find)
    , replace_buf_ (initial_replace)
    , on_find_next_(std::move(on_find_next))
    , on_replace_  (std::move(on_replace))
    , on_replace_all_(std::move(on_replace_all))
{}

void ReplaceDialog::show(Renderer& renderer,
                         const std::string& initial_find,
                         const std::string& initial_replace,
                         ReplaceCallback    on_find_next,
                         ReplaceCallback    on_replace,
                         ReplaceCallback    on_replace_all,
                         std::function<void()> on_background_refresh)
{
    ReplaceDialog dlg(initial_find, initial_replace,
                      std::move(on_find_next),
                      std::move(on_replace),
                      std::move(on_replace_all));
    if (on_background_refresh)
        dlg.setBackgroundRefresh(std::move(on_background_refresh));
    dlg.run(renderer);
}

void ReplaceDialog::onInit()
{
    setFocusCount(static_cast<int>(Focus::_count));
    setFocus(static_cast<int>(Focus::FIND));
    setButtonRowFocusIndex(static_cast<int>(Focus::BTN_ROW));

    // ── Inputs ────────────────────────────────────────────────────────────────
    addInput({
        .focus_index = static_cast<int>(Focus::FIND),
        .field_x = 18, .field_y = 2, .field_w = 40,
        .label   = "Find what:",
        .label_x = 3, .label_y = 2,
        .buffer  = find_buf_,
    });
    addInput({
        .focus_index = static_cast<int>(Focus::REPLACE),
        .field_x = 18, .field_y = 4, .field_w = 40,
        .label   = "Replace with:",
        .label_x = 3, .label_y = 4,
        .buffer  = replace_buf_,
    });

    // ── Buttons ───────────────────────────────────────────────────────────────
    // width=64, margins=4 each side, 3-char gaps between buttons
    // Cols: Find Next(12) gap(3) Replace(10) gap(3) Replace All(14) gap(3) Close(9)
    // x:    4              16-18  19          29-31  32              46-48  49
    addButtons(ButtonRow{
        .buttons = {
            Button{
                .label = " F&ind Next ",
                .x = 4, .y = 8,
                .on_activate = [this]() -> HandleResult {
                    on_find_next_(find_buf_, replace_buf_, status_);
                    return HandleResult::CONTINUE;
                }
            },
            Button{
                .label = " &Replace ",
                .x = 19, .y = 8,
                .on_activate = [this]() -> HandleResult {
                    on_replace_(find_buf_, replace_buf_, status_);
                    return HandleResult::CONTINUE;
                }
            },
            Button{
                .label = " Replace &All ",
                .x = 32, .y = 8,
                .on_activate = [this]() -> HandleResult {
                    on_replace_all_(find_buf_, replace_buf_, status_);
                    return HandleResult::CONTINUE;
                }
            },
            Button{
                .label = " &Close ",
                .x = 49, .y = 8,
                .on_activate = [this]() -> HandleResult {
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

void ReplaceDialog::onDraw(Renderer& renderer, int startx, int starty)
{
    // Status line at row 6: match counter or informational message.
    // Always paint a full-width blank first to clear stale text.
    renderer.drawText(startx + 3, starty + 6,
                      std::string(58, ' '), Renderer::CP_DIALOG);
    if (!status_.empty())
        renderer.drawText(startx + 3, starty + 6,
                          status_, Renderer::CP_STATUS_BAR_HIGHLIGHT);
}
