#include "TargetDialog.h"
#include <string>

static const char* kTypeKeys[] = {
    "executable", "static_library", "shared_library"
};

// ── Constructor ───────────────────────────────────────────────────────────────

TargetDialog::TargetDialog(const ProjectTarget& initial, bool is_new)
    : DialogBase(is_new ? "Add Target" : "Target Properties", W, H)
    , is_new_(is_new)
    , name_(initial.name)
{
    for (int i = 0; i < 3; ++i)
        if (std::string(kTypeKeys[i]) == initial.type) {
            type_sel_ = type_cur_ = i;
            break;
        }
}

// ── Static factory ────────────────────────────────────────────────────────────

bool TargetDialog::show(Renderer& renderer, ProjectTarget& target, bool is_new)
{
    TargetDialog dlg(target, is_new);
    DialogResult res = dlg.run(renderer);
    if (res.accepted()) {
        target.name = dlg.name_;
        target.type = kTypeKeys[dlg.type_sel_];
    }
    return res.accepted();
}

// ── onInit ────────────────────────────────────────────────────────────────────

void TargetDialog::onInit()
{
    // Group 0: Name text field (cursor placement formula needs box_x/box_y)
    {
        FocusGroup g;
        g.title       = "";
        g.hotkey      = '\0';
        g.box_x       = NAME_FIELD_X - 2;  // 1  → cursor lands at col NAME_FIELD_X
        g.box_y       = NAME_FIELD_Y - 1;  // 2  → cursor lands at row NAME_FIELD_Y
        g.box_w       = 0; g.box_h = 0;    // no box drawn
        g.text_buffer = &name_;
        addGroup(std::move(g));
    }

    // Group 1: Target type — RadioList (x/y are relative to dialog origin)
    {
        FocusGroup g;
        g.title  = " Target type ";
        g.hotkey = '\0';
        g.box_x  = TYPE_BOX_X; g.box_y = TYPE_BOX_Y;
        g.box_w  = TYPE_BOX_W; g.box_h = TYPE_BOX_H;
        g.radiolists.push_back({
            {"Executable", "Static library", "Shared library"},
            type_sel_, type_cur_,
            TYPE_BOX_X + 2, TYPE_BOX_Y + 1, 3
        });
        addGroup(std::move(g));
    }

    addButtons(ButtonRow{
        .buttons = {
            Button{
                .label = " &Ok ",
                .x = W / 2 - 9, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    result().accept();
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

    setGroupFocus(GRP_NAME);
    setGroupBtnFocus(0);
}

// ── onDraw ────────────────────────────────────────────────────────────────────

void TargetDialog::onDraw(Renderer& renderer, int startx, int starty)
{
    renderer.drawText(startx + NAME_FIELD_X, starty + NAME_FIELD_Y - 1,
                      "Name:", Renderer::CP_DIALOG);

    bool name_focused = (getFocusedGroup() == GRP_NAME);
    std::string field(NAME_FIELD_W, ' ');
    int disp = std::min((int)name_.size(), NAME_FIELD_W);
    field.replace(0, disp, name_.substr(0, disp));
    renderer.drawText(startx + NAME_FIELD_X, starty + NAME_FIELD_Y, field,
                      name_focused ? Renderer::CP_LIST_BOX : Renderer::CP_DIALOG);
}
