#include "PickTargetDialog.h"

PickTargetDialog::PickTargetDialog(const std::vector<ProjectTarget>& targets,
                                   const std::string& dialog_title,
                                   int exclude_idx)
    : DialogBase(dialog_title, W, H)
{
    for (int i = 0; i < (int)targets.size(); ++i) {
        if (i == exclude_idx) continue;
        const auto& t = targets[i];
        std::string abbr = (t.type == "executable")     ? "exe"
                         : (t.type == "static_library") ? "lib" : "dll";
        items_.push_back(t.name + "  [" + abbr + "]");
        real_idx_.push_back(i);
    }
}

int PickTargetDialog::show(Renderer& renderer,
                            const std::vector<ProjectTarget>& targets,
                            const std::string& dialog_title,
                            int exclude_idx)
{
    PickTargetDialog dlg(targets, dialog_title, exclude_idx);
    if (dlg.items_.empty()) return -1;
    DialogResult res = dlg.run(renderer);
    if (!res.accepted()) return -1;
    return dlg.real_idx_[dlg.cursor_];
}

void PickTargetDialog::onInit()
{
    {
        FocusGroup g;
        g.box_x = LIST_BOX_X; g.box_y = LIST_BOX_Y;
        g.box_w = LIST_BOX_W; g.box_h = LIST_BOX_H;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    addButtons(ButtonRow{
        .buttons = {
            Button{
                .label = " &Select ",
                .x = W / 2 - 10, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    if (!items_.empty()) result().accept();
                    return HandleResult::CLOSE;
                }
            },
            Button{
                .label = " &Cancel ",
                .x = W / 2 + 1, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    result().cancel();
                    return HandleResult::CLOSE;
                }
            },
        }
    });

    setGroupFocus(GRP_LIST);
    setGroupBtnFocus(0);
}

void PickTargetDialog::onDraw(Renderer& renderer, int startx, int starty)
{
    bool list_focused = (getFocusedGroup() == GRP_LIST);
    int inner_w = LIST_BOX_W - 2;

    for (int i = 0; i < VISIBLE; ++i) {
        int idx = scroll_ + i;
        if (idx >= (int)items_.size()) break;
        bool sel = list_focused && (idx == cursor_);
        std::string text = items_[idx];
        if ((int)text.size() < inner_w)
            text += std::string(inner_w - (int)text.size(), ' ');
        else
            text = text.substr(0, inner_w);
        renderer.drawText(startx + LIST_BOX_X + 1,
                          starty + LIST_BOX_Y + 1 + i,
                          text,
                          sel ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG,
                          sel ? A_BOLD : A_NORMAL);
    }
}

HandleResult PickTargetDialog::onKey(wint_t ch)
{
    if (ch == KEY_UP) {
        if (cursor_ > 0) {
            --cursor_;
            if (cursor_ < scroll_) scroll_ = cursor_;
        }
        return HandleResult::CONTINUE;
    }
    if (ch == KEY_DOWN) {
        if (cursor_ < (int)items_.size() - 1) {
            ++cursor_;
            if (cursor_ >= scroll_ + VISIBLE)
                scroll_ = cursor_ - VISIBLE + 1;
        }
        return HandleResult::CONTINUE;
    }
    if (ch == KEY_ENTER || ch == 10 || ch == 13) {
        if (!items_.empty()) {
            result().accept();
            return HandleResult::CLOSE;
        }
    }
    return HandleResult::CONTINUE;
}
