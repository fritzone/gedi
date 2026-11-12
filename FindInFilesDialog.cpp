#include "FindInFilesDialog.h"
#include "curses_compat.h"
#include <algorithm>

FindInFilesDialog::FindInFilesDialog(const FindInFilesOptions& opts,
                                     const std::vector<std::pair<std::string, std::string>>& scopes)
    : DialogBase("Find in Files", W, H)
    , opts_(opts)
    , scopes_(scopes)
{
    opts_.scope   = std::clamp(opts_.scope, 0, std::max(0, (int)scopes_.size() - 1));
    scope_cursor_ = opts_.scope;
    fresh_        = !opts_.text.empty();
}

bool FindInFilesDialog::show(Renderer& renderer, FindInFilesOptions& opts,
                             const std::vector<std::pair<std::string, std::string>>& scopes)
{
    FindInFilesDialog dlg(opts, scopes);
    if (!dlg.run(renderer).accepted()) return false;
    opts = dlg.opts_;
    return true;
}

void FindInFilesDialog::onInit()
{
    // Group 0: the text to find (no box; the cursor lands at FIELD_X, FIELD_Y)
    {
        FocusGroup g;
        g.title  = "";
        g.hotkey = 't';
        g.box_x  = FIELD_X - 2; g.box_y = FIELD_Y - 1;
        g.box_w  = 0; g.box_h = 0;
        g.text_buffer = &opts_.text;
        addGroup(std::move(g));
    }
    // Group 1: how to match
    {
        FocusGroup g;
        g.title  = " Options ";
        g.hotkey = 'o';
        g.box_x  = OPT_X; g.box_y = BOX_Y; g.box_w = OPT_W; g.box_h = BOX_H;
        g.checkboxes.push_back({"Match case",         opts_.match_case, OPT_X + 2, BOX_Y + 1});
        g.checkboxes.push_back({"Whole words",        opts_.whole_word, OPT_X + 2, BOX_Y + 2});
        g.checkboxes.push_back({"Regular expression", opts_.regex,      OPT_X + 2, BOX_Y + 3});
        addGroup(std::move(g));
    }
    // Group 2: where (a group of its own: a FocusGroup with checkboxes never
    // routes keys to a RadioList)
    {
        FocusGroup g;
        g.title  = " Where ";
        g.hotkey = 'w';
        g.box_x  = WHERE_X; g.box_y = BOX_Y; g.box_w = WHERE_W; g.box_h = BOX_H;
        std::vector<std::string> labels;
        for (const auto& s : scopes_) labels.push_back(s.first);
        g.radiolists.push_back({labels, opts_.scope, scope_cursor_,
                                WHERE_X + 2, BOX_Y + 1, BOX_H - 2});
        addGroup(std::move(g));
    }

    addButtons(ButtonRow{.buttons = {
        Button{
            .label = " &Find ",
            .x = W / 2 - 10, .y = BTN_Y,
            .on_activate = [this]() -> HandleResult {
                if (opts_.text.empty()) { setGroupFocus(GRP_TEXT); return HandleResult::CONTINUE; }
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
    }});

    setGroupFocus(GRP_TEXT);
    setGroupBtnFocus(0);
}

void FindInFilesDialog::onDraw(Renderer& renderer, int startx, int starty)
{
    renderer.drawText(startx + FIELD_X, starty + FIELD_Y - 1, "Text to find:", Renderer::CP_DIALOG);

    // The field shows the end of a text longer than it (where the cursor is).
    const bool focused = getFocusedGroup() == GRP_TEXT;
    std::string shown = opts_.text;
    if ((int)shown.size() > FIELD_W - 1) shown = shown.substr(shown.size() - (FIELD_W - 1));
    std::string field(FIELD_W, ' ');
    field.replace(0, shown.size(), shown);
    renderer.drawText(startx + FIELD_X, starty + FIELD_Y, field,
                      focused ? Renderer::CP_LIST_BOX : Renderer::CP_DIALOG);
    if (fresh_ && focused)   // shown selected: typing replaces it
        renderer.drawText(startx + FIELD_X, starty + FIELD_Y, shown, Renderer::CP_SELECTION);

    // What the chosen place covers.
    renderer.drawText(startx + 3, starty + NOTE_Y, std::string(W - 6, ' '), Renderer::CP_DIALOG);
    if (opts_.scope >= 0 && opts_.scope < (int)scopes_.size()) {
        std::string note = scopes_[opts_.scope].second;
        if ((int)note.size() > W - 6) note = note.substr(0, W - 9) + "...";
        renderer.drawText(startx + 3, starty + NOTE_Y, note, Renderer::CP_DIALOG);
    }
}

HandleResult FindInFilesDialog::onKey(wint_t ch)
{
    // Enter in the text field searches.
    if (getFocusedGroup() == GRP_TEXT && (ch == KEY_ENTER || ch == 10 || ch == 13)) {
        if (!opts_.text.empty()) activateButtonByIndex(0);
        return HandleResult::CONTINUE;
    }
    return HandleResult::CONTINUE;
}

bool FindInFilesDialog::onKeyFirst(wint_t ch)
{
    // Enter searches from anywhere but the buttons (Space ticks the boxes).
    const bool enter = ch == KEY_ENTER || ch == 10 || ch == 13;
    if (enter && !inButtonRow() && getFocusedGroup() != GRP_TEXT) {
        if (getFocusedGroup() == GRP_WHERE) opts_.scope = scope_cursor_;   // the one highlighted
        if (!opts_.text.empty()) activateButtonByIndex(0);
        return true;
    }
    if (!fresh_ || getFocusedGroup() != GRP_TEXT || inButtonRow()) return false;
    if (ch == KEY_ENTER || ch == 10 || ch == 13) return false;     // search with it as it is
    fresh_ = false;
    if (ch > 31 && ch < KEY_MIN) { opts_.text.clear(); return false; }   // the base types it
    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8 || ch == KEY_DC) { opts_.text.clear(); return true; }
    return false;                                                   // arrows, Tab: keep it
}

bool FindInFilesDialog::onPlaceCursor(Renderer& renderer, int sx, int sy)
{
    if (getFocusedGroup() != GRP_TEXT || inButtonRow()) return false;
    renderer.showCursor();
    move(sy + FIELD_Y, sx + FIELD_X + std::min((int)opts_.text.size(), FIELD_W - 1));
    return true;
}
