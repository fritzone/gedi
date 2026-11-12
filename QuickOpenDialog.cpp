#include "QuickOpenDialog.h"
#include "FileSearch.h"
#include "curses_compat.h"
#include <algorithm>

QuickOpenDialog::QuickOpenDialog(const std::vector<QuickOpenItem>& items, const std::string& query,
                                 int w, int h, Mode mode)
    : DialogBase(mode == Mode::Files ? "Quick Open" : "Go to Symbol", w, h)
    , w_(w), h_(h)
    , mode_(mode)
    , items_(items)
    , query_(query)
{}

int QuickOpenDialog::show(Renderer& renderer, const std::vector<QuickOpenItem>& items,
                          std::string& query, int& line, Mode mode)
{
    const int w = std::clamp(renderer.getWidth() - 8, 50, 100);
    const int h = std::clamp(renderer.getHeight() - 4, 12, 28);
    QuickOpenDialog dlg(items, query, w, h, mode);
    const bool ok = dlg.run(renderer).accepted();
    query = dlg.query_;
    if (!ok || dlg.chosen_ < 0) return -1;
    std::string pattern;
    dlg.splitQuery(pattern, line);
    return dlg.chosen_;
}

void QuickOpenDialog::splitQuery(std::string& pattern, int& line) const
{
    pattern = query_;
    line = 0;
    if (mode_ != Mode::Files) return;   // "a::b" is a name, not a line
    const std::size_t colon = query_.rfind(':');
    if (colon == std::string::npos || colon + 1 >= query_.size()) {
        if (colon != std::string::npos && colon + 1 == query_.size()) pattern = query_.substr(0, colon);
        return;
    }
    const std::string num = query_.substr(colon + 1);
    if (!std::all_of(num.begin(), num.end(), [](unsigned char c) { return std::isdigit(c); })) return;
    pattern = query_.substr(0, colon);
    try { line = std::stoi(num); } catch (...) { line = 0; }
}

void QuickOpenDialog::onInit()
{
    {
        FocusGroup g;
        g.title  = "";
        g.box_x  = FIELD_X - 2; g.box_y = FIELD_Y - 1;
        g.box_w  = 0; g.box_h = 0;
        g.text_buffer = &query_;
        addGroup(std::move(g));
    }
    addButtons(ButtonRow{.buttons = {
        Button{
            .label = " &Open ",
            .x = w_ / 2 - 10, .y = h_ - 3,
            .on_activate = [this]() -> HandleResult {
                if (cursor_ < 0 || cursor_ >= (int)matches_.size()) return HandleResult::CONTINUE;
                chosen_ = matches_[cursor_].idx;
                result().accept();
                return HandleResult::CLOSE;
            }
        },
        Button{
            .label = " &Cancel ",
            .x = w_ / 2 + 1, .y = h_ - 3,
            .on_activate = [this]() -> HandleResult {
                result().cancel();
                return HandleResult::CLOSE;
            }
        },
    }});
    setGroupFocus(0);
    setGroupBtnFocus(0);
    refilter();
}

void QuickOpenDialog::refilter()
{
    std::string pattern; int line;
    splitQuery(pattern, line);
    if (pattern == filtered_for_) return;
    filtered_for_ = pattern;

    matches_.clear();
    if (pattern.find_first_not_of(' ') == std::string::npos) {
        // Nothing typed: the items in their own order (open, recent, the rest).
        for (int i = 0; i < (int)items_.size() && i < MAX_SHOWN; ++i) matches_.push_back({i, {}});
    } else {
        std::vector<std::pair<int, Match>> scored;
        for (int i = 0; i < (int)items_.size(); ++i) {
            std::vector<int> pos;
            const int sc = filesearch::fuzzyScore(pattern, items_[i].rel, &pos);
            if (sc < 0) continue;
            scored.push_back({sc + (items_[i].open ? 3 : 0), Match{i, std::move(pos)}});
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
        for (auto& s : scored) {
            if ((int)matches_.size() >= MAX_SHOWN) break;
            matches_.push_back(std::move(s.second));
        }
    }
    cursor_ = 0;
    top_    = 0;
}

void QuickOpenDialog::moveCursor(int delta)
{
    if (matches_.empty()) return;
    cursor_ = std::clamp(cursor_ + delta, 0, (int)matches_.size() - 1);
}

void QuickOpenDialog::onDraw(Renderer& renderer, int startx, int starty)
{
    refilter();   // the base edited the query since the last frame

    renderer.drawText(startx + FIELD_X, starty + FIELD_Y - 1,
                      mode_ == Mode::Files ? "File name (name:line goes to a line):" : "Symbol name:",
                      Renderer::CP_DIALOG);
    std::string shown = query_;
    if ((int)shown.size() > fieldW() - 1) shown = shown.substr(shown.size() - (fieldW() - 1));
    std::string field(fieldW(), ' ');
    field.replace(0, shown.size(), shown);
    renderer.drawText(startx + FIELD_X, starty + FIELD_Y, field, Renderer::CP_LIST_BOX);

    // The list: "• name   dir/" - the name first, so the part that tells files
    // apart is never cut off; matched characters stand out.
    const int rows = listRows();
    const int lx = startx + FIELD_X, lw = fieldW();
    if (cursor_ < top_) top_ = cursor_;
    if (cursor_ >= top_ + rows) top_ = cursor_ - rows + 1;
    for (int r = 0; r < rows; ++r) {
        const int y = starty + LIST_Y + r;
        const int mi = top_ + r;
        const bool sel = mi == cursor_;
        const int pair = sel ? Renderer::CP_LIST_SELECTED : Renderer::CP_LIST_BOX;
        renderer.drawText(lx, y, std::string(lw, ' '), Renderer::CP_LIST_BOX);
        if (mi >= (int)matches_.size()) continue;

        const QuickOpenItem& it = items_[matches_[mi].idx];
        if (mode_ == Mode::Symbols) {
            // "name    kind  scope", the matched letters marked in the name
            renderer.drawText(lx, y, std::string(lw, ' '), pair);
            const int name_x = lx + 2, avail = lw - 3;
            const std::string name = (int)it.rel.size() > avail ? it.rel.substr(0, avail) : it.rel;
            renderer.drawText(name_x, y, name, pair);
            const int det_x = name_x + std::max((int)name.size() + 3, 26);
            if (!it.detail.empty() && det_x < lx + lw - 4) {
                const int det_avail = lx + lw - 1 - det_x;
                renderer.drawText(det_x, y, (int)it.detail.size() > det_avail ? it.detail.substr(0, det_avail)
                                                                              : it.detail, pair);
            }
            for (int p : matches_[mi].pos)
                if (p < (int)name.size())
                    renderer.drawText(name_x + p, y, std::string(1, name[p]), Renderer::CP_SELECTION);
            continue;
        }
        const std::size_t slash = it.rel.rfind('/');
        const int base_start = slash == std::string::npos ? 0 : (int)slash + 1;
        const std::string name = it.rel.substr(base_start);
        const std::string dir  = it.rel.substr(0, base_start);

        renderer.drawText(lx, y, std::string(lw, ' '), pair);
        renderer.drawText(lx, y, it.open ? "\xe2\x80\xa2" : " ", pair);   // • = in a window
        const int name_x = lx + 2;
        const int avail  = lw - 3;
        std::string name_shown = (int)name.size() > avail ? name.substr(0, avail) : name;
        renderer.drawText(name_x, y, name_shown, pair);
        int dir_x = name_x + (int)name_shown.size() + 3;
        std::string dir_shown;
        if (!dir.empty() && dir_x < lx + lw - 4) {
            const int dir_avail = lx + lw - 1 - dir_x;
            dir_shown = (int)dir.size() > dir_avail ? "..." + dir.substr(dir.size() - (dir_avail - 3)) : dir;
            renderer.drawText(dir_x, y, dir_shown, pair);
        }
        // Matched characters marked as Find in Files marks its matches.
        // (Byte offsets; paths are ASCII nearly always.)
        for (int p : matches_[mi].pos) {
            if (p >= base_start) {
                const int off = p - base_start;
                if (off < (int)name_shown.size())
                    renderer.drawText(name_x + off, y, std::string(1, name[off]), Renderer::CP_SELECTION);
            } else if (dir_shown == dir && p < (int)dir.size()) {
                renderer.drawText(dir_x + p, y, std::string(1, dir[p]), Renderer::CP_SELECTION);
            }
        }
    }

    // Count, and the scroll position for long lists.
    std::string info = std::to_string(matches_.size()) + (matches_.size() >= (size_t)MAX_SHOWN ? "+" : "") +
                       " of " + std::to_string(items_.size()) + (mode_ == Mode::Files ? " files" : " symbols");
    renderer.drawText(lx, starty + LIST_Y + rows, std::string(lw, ' '), Renderer::CP_DIALOG);
    renderer.drawText(lx, starty + LIST_Y + rows, info, Renderer::CP_DIALOG);
    if ((int)matches_.size() > rows) {
        const std::string pos = std::to_string(cursor_ + 1) + "/" + std::to_string(matches_.size());
        renderer.drawText(lx + lw - (int)pos.size(), starty + LIST_Y + rows, pos, Renderer::CP_DIALOG);
    }
}

HandleResult QuickOpenDialog::onKey(wint_t ch)
{
    switch (ch) {
    case KEY_UP:    moveCursor(-1); break;
    case KEY_DOWN:  moveCursor(+1); break;
    case KEY_PPAGE: moveCursor(-listRows()); break;
    case KEY_NPAGE: moveCursor(+listRows()); break;
    case KEY_ENTER: case 10: case 13:
        if (!matches_.empty()) activateButtonByIndex(0);
        break;
    default: break;
    }
    return HandleResult::CONTINUE;
}

bool QuickOpenDialog::onPlaceCursor(Renderer& renderer, int sx, int sy)
{
    if (inButtonRow()) return false;
    renderer.showCursor();
    move(sy + FIELD_Y, sx + FIELD_X + std::min((int)query_.size(), fieldW() - 1));
    return true;
}

bool QuickOpenDialog::onMouseClick(const MEVENT& ev, int startx, int starty)
{
    // A click on a file opens it.
    const int row = ev.y - (starty + LIST_Y);
    if (row < 0 || row >= listRows()) return false;
    if (ev.x < startx + FIELD_X || ev.x >= startx + FIELD_X + fieldW()) return false;
    const int mi = top_ + row;
    if (mi >= (int)matches_.size()) return true;
    cursor_ = mi;
    activateButtonByIndex(0);
    return true;
}
