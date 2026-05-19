#include "AddFileDialog.h"
#include "FileBrowser.h"
#include <ncurses.h>
#include <algorithm>

static void insertUtf8At(std::string& buf, int& pos, wint_t ch)
{
    std::string enc;
    if      (ch < 0x80)    { enc += static_cast<char>(ch); }
    else if (ch < 0x800)   { enc += static_cast<char>(0xC0 | (ch >> 6));
                              enc += static_cast<char>(0x80 | (ch & 0x3F)); }
    else if (ch < 0x10000) { enc += static_cast<char>(0xE0 | (ch >> 12));
                              enc += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                              enc += static_cast<char>(0x80 | (ch & 0x3F)); }
    else                   { enc += static_cast<char>(0xF0 | (ch >> 18));
                              enc += static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
                              enc += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                              enc += static_cast<char>(0x80 | (ch & 0x3F)); }
    buf.insert(pos, enc);
    pos += (int)enc.size();
}

static void eraseUtf8Before(std::string& buf, int& pos)
{
    if (pos <= 0) return;
    int p = pos - 1;
    while (p > 0 && (static_cast<unsigned char>(buf[p]) & 0xC0) == 0x80) --p;
    buf.erase(p, pos - p);
    pos = p;
}

static void eraseUtf8At(std::string& buf, int pos)
{
    if (pos < 0 || pos >= (int)buf.size()) return;
    int end = pos + 1;
    while (end < (int)buf.size() && (static_cast<unsigned char>(buf[end]) & 0xC0) == 0x80) ++end;
    buf.erase(pos, end - pos);
}

static int utf8StepLeft(const std::string& buf, int pos)
{
    if (pos <= 0) return 0;
    int p = pos - 1;
    while (p > 0 && (static_cast<unsigned char>(buf[p]) & 0xC0) == 0x80) --p;
    return p;
}

static int utf8StepRight(const std::string& buf, int pos)
{
    if (pos >= (int)buf.size()) return pos;
    int p = pos + 1;
    while (p < (int)buf.size() && (static_cast<unsigned char>(buf[p]) & 0xC0) == 0x80) ++p;
    return p;
}

static void adjustScroll(int cursor, int field_w, int& scroll)
{
    if (cursor < scroll) scroll = cursor;
    if (cursor > scroll + field_w - 1) scroll = cursor - field_w + 1;
    if (scroll < 0) scroll = 0;
}

// ═══════════════════════════════════════════════════════════════════════════════

AddFileDialog::AddFileDialog(Renderer& renderer, const std::string& project_dir,
                             AddFileInfo& out)
    : DialogBase("Add File to Project", W, H)
    , renderer_    (renderer)
    , project_dir_ (project_dir)
    , info_        (out)
    , type_cursor_ (out.is_new ? 0 : 1)
    , file_cursor_ ((int)out.filepath.size())
    , file_scroll_ (0)
{
    adjustScroll(file_cursor_, FIELD_W, file_scroll_);
}

// ── Static factory ────────────────────────────────────────────────────────────

bool AddFileDialog::show(Renderer& renderer, const std::string& project_dir, AddFileInfo& out)
{
    AddFileDialog dlg(renderer, project_dir, out);
    return dlg.run(renderer).accepted();
}

// ── onInit ────────────────────────────────────────────────────────────────────

void AddFileDialog::onInit()
{
    // Group 0: Type
    {
        FocusGroup g;
        g.title = " Type ";
        g.box_x = 2; g.box_y = TYPE_BOX_Y;
        g.box_w = W - 4; g.box_h = TYPE_BOX_H;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    // Group 1: File
    {
        FocusGroup g;
        g.title = " File ";
        g.box_x = 2; g.box_y = FILE_BOX_Y;
        g.box_w = W - 4; g.box_h = FILE_BOX_H;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    addButtons(ButtonRow{.buttons = {
        Button{
            .label = " &Add ",
            .x = BTN_ADD_X, .y = BTN_Y,
            .on_activate = [this]() -> HandleResult {
                if (info_.filepath.empty()) return HandleResult::CONTINUE;
                info_.is_new = (type_cursor_ == 0);
                result().accept();
                return HandleResult::CLOSE;
            }
        },
        Button{
            .label = " &Cancel ",
            .x = BTN_CANCEL_X, .y = BTN_Y,
            .on_activate = [this]() -> HandleResult {
                result().cancel();
                return HandleResult::CLOSE;
            }
        },
        Button{
            .label = " &Browse ",
            .x = BROWSE_BTN_X, .y = BROWSE_BTN_Y,
            .on_activate = [this]() -> HandleResult {
                std::string chosen = FileBrowser::open(renderer_);
                if (!chosen.empty()) {
                    info_.filepath = chosen;
                    file_cursor_   = (int)chosen.size();
                    file_scroll_   = 0;
                    adjustScroll(file_cursor_, FIELD_W, file_scroll_);
                    type_cursor_   = 1;  // switch to Existing
                }
                return HandleResult::CONTINUE;
            }
        },
    }});

    setGroupFocus(GRP_TYPE);
    setGroupBtnFocus(0);
}

// ── onDraw ────────────────────────────────────────────────────────────────────

void AddFileDialog::onDraw(Renderer& renderer, int sx, int sy)
{
    const int inner_x = sx + 2;

    // Type radio buttons
    {
        const int fy = sy + TYPE_BOX_Y + 1;
        bool grp_focused = (getFocusedGroup() == GRP_TYPE);
        const char* labels[] = {"New file", "Existing file"};
        int rx = inner_x + 2;
        for (int i = 0; i < 2; ++i) {
            bool sel = (type_cursor_ == i);
            bool cur = grp_focused && sel;
            std::string mark = sel ? "(•)" : "( )";
            renderer.drawText(rx, fy, mark + " " + labels[i],
                              cur ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
            rx += (int)(mark.size() + 1 + strlen(labels[i]) + 2);
        }
    }

    // File path field
    {
        const int fy = sy + FILE_BOX_Y + 1;
        adjustScroll(file_cursor_, FIELD_W, file_scroll_);
        renderer.drawText(inner_x + FIELD_X, fy,
                          std::string(FIELD_W, ' '), Renderer::CP_LIST_BOX);
        if (!info_.filepath.empty() && (int)info_.filepath.size() > file_scroll_) {
            renderer.drawText(inner_x + FIELD_X, fy,
                              info_.filepath.substr(file_scroll_, FIELD_W),
                              Renderer::CP_LIST_BOX);
        }
    }
}

// ── onPlaceCursor ─────────────────────────────────────────────────────────────

bool AddFileDialog::onPlaceCursor(Renderer& renderer, int sx, int sy)
{
    if (getFocusedGroup() == GRP_FILE) {
        renderer.showCursor();
        move(sy + FILE_BOX_Y + 1,
             sx + 2 + FIELD_X + (file_cursor_ - file_scroll_));
        return true;
    }
    return false;
}

// ── onKey ─────────────────────────────────────────────────────────────────────

HandleResult AddFileDialog::onKey(wint_t ch)
{
    int grp = getFocusedGroup();

    if (grp == GRP_TYPE) {
        if (ch == KEY_LEFT  && type_cursor_ > 0) --type_cursor_;
        if (ch == KEY_RIGHT && type_cursor_ < 1) ++type_cursor_;
        if ((ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13) && type_cursor_ < 1)
            ++type_cursor_;
        else if ((ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13) && type_cursor_ > 0)
            --type_cursor_;
        return HandleResult::CONTINUE;
    }

    if (grp == GRP_FILE) {
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            eraseUtf8Before(info_.filepath, file_cursor_);
        } else if (ch == KEY_DC) {
            eraseUtf8At(info_.filepath, file_cursor_);
        } else if (ch == KEY_LEFT) {
            file_cursor_ = utf8StepLeft(info_.filepath, file_cursor_);
        } else if (ch == KEY_RIGHT) {
            file_cursor_ = utf8StepRight(info_.filepath, file_cursor_);
        } else if (ch == KEY_HOME) {
            file_cursor_ = 0;
        } else if (ch == KEY_END) {
            file_cursor_ = (int)info_.filepath.size();
        } else if (ch > 31 && ch < KEY_MIN) {
            insertUtf8At(info_.filepath, file_cursor_, ch);
        }
        return HandleResult::CONTINUE;
    }

    return HandleResult::CONTINUE;
}

// ── onTab ─────────────────────────────────────────────────────────────────────
// Tab cycle: Type → File → Add → Cancel → Type  (Browse skipped; Alt+B only)

bool AddFileDialog::onTab(bool forward)
{
    const int btn_row = groupCount();  // = 2

    if (forward) {
        // Cancel → Type (skip Browse in the default row cycle)
        if (inButtonRow() && getBtnInnerFocus() == BTN_IDX_CANCEL) {
            setGroupFocus(GRP_TYPE);
            return true;
        }
    } else {
        // Type → Cancel (skip Browse going backward)
        if (getFocusedGroup() == GRP_TYPE) {
            setGroupFocus(btn_row);
            setGroupBtnFocus(BTN_IDX_CANCEL);
            return true;
        }
    }

    return false;
}
