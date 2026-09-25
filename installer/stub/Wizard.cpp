#include "Wizard.h"

#include "curses_compat.h"
#include "FileBrowser.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

using nlohmann::json;

namespace {

// CP437 shade block - the Borland desktop pattern. curses_compat maps the code
// point onto the matching VGA glyph.
const char* SHADE = "\xe2\x96\x92";      // U+2592
const char* BAR_FULL = "\xe2\x96\x88";   // U+2588
const char* BAR_EMPTY = "\xe2\x96\x91";  // U+2591

std::string repeat(const char* glyph, int n) {
    std::string s;
    for (int i = 0; i < n; ++i) s += glyph;
    return s;
}

std::string center(const std::string& s, int w) {
    if ((int)s.size() >= w) return s.substr(0, w);
    const int pad = (w - (int)s.size()) / 2;
    return std::string(pad, ' ') + s;
}

} // namespace

Wizard::Wizard(Renderer& renderer, const payload::Payload& pl)
    : r_(renderer), pl_(pl), spec_(pl.spec()) {

    // ---- target directory --------------------------------------------------
    const json install = spec_.value("install", json::object());
    dir_ = inst::nativePath(inst::expandEnv(
        install.value("default_dir", std::string("%LOCALAPPDATA%\\Programs\\App"))));
    dir_cursor_ = (int)dir_.size();
    make_start_menu_ = install.value("start_menu", true);
    make_desktop_    = install.value("desktop_shortcut", false);

    // ---- components --------------------------------------------------------
    for (const auto& c : spec_.value("components", json::array())) {
        Component comp;
        comp.id          = c.value("id", std::string());
        comp.name        = c.value("name", comp.id);
        comp.description = c.value("description", std::string());
        comp.required    = c.value("required", false);
        comp.selected    = comp.required || c.value("selected", true);
        for (const auto& e : pl_.entries())
            if (e.component == comp.id) comp.bytes += e.usize;
        if (!comp.id.empty()) components_.push_back(std::move(comp));
    }

    // ---- license -----------------------------------------------------------
    const json ui = spec_.value("ui", json::object());
    license_lines_ = wrap(ui.value("license", std::string()), contentW());

    // ---- what to offer to launch at the end --------------------------------
    for (const auto& sc : spec_.value("shortcuts", json::array())) {
        if (!sc.contains("target")) continue;
        launch_target_ = sc["target"].get<std::string>();
        break;
    }

    setPage(P_WELCOME);
}

// ---------------------------------------------------------------------------
//  Geometry and chrome
// ---------------------------------------------------------------------------
int Wizard::panelX() const { return (r_.getWidth()  - W) / 2; }
int Wizard::panelY() const { return (r_.getHeight() - H) / 2; }

void Wizard::drawDesktop() {
    const int w = r_.getWidth(), h = r_.getHeight();

    const json ui = spec_.value("ui", json::object());
    const json product = spec_.value("product", json::object());
    const std::string title = ui.value("title", product.value("name", std::string("Setup")));

    // Top bar, shaded desktop, bottom hint bar - the Turbo layout.
    r_.drawText(0, 0, std::string(w, ' '), Renderer::CP_MENU_BAR);
    r_.drawText(2, 0, title, Renderer::CP_MENU_BAR, A_BOLD);
    const std::string ver = product.value("version", std::string());
    if (!ver.empty())
        r_.drawText(w - (int)ver.size() - 3, 0, "v" + ver, Renderer::CP_MENU_BAR);

    // CP_DESKTOP is the pair gedi fills its own empty desktop with (blue on
    // white), so setup and the editor show the same backdrop.
    const std::string row = repeat(SHADE, w);
    for (int y = 1; y < h - 1; ++y) r_.drawText(0, y, row, Renderer::CP_DESKTOP);
}

void Wizard::drawPanel(const std::string& title) {
    r_.drawShadow(panelX(), panelY(), W, H);
    r_.drawBoxWithTitle(panelX(), panelY(), W, H, Renderer::CP_DIALOG, Renderer::DOUBLE,
                        " " + title + " ", Renderer::CP_DIALOG_TITLE, A_BOLD);
    // Blank the interior so a previous page cannot show through.
    for (int y = panelY() + 1; y < panelY() + H - 1; ++y)
        r_.drawText(panelX() + 1, y, std::string(W - 2, ' '), Renderer::CP_DIALOG);
}

void Wizard::drawHints(const std::string& hints) {
    const int w = r_.getWidth(), h = r_.getHeight();
    r_.drawText(0, h - 1, std::string(w, ' '), Renderer::CP_STATUS_BAR);
    r_.drawText(2, h - 1, hints, Renderer::CP_STATUS_BAR);
}

void Wizard::drawButtons() {
    if (buttons_.empty()) return;

    int total = 0;
    for (const auto& b : buttons_) total += (int)b.text.size() + 2;
    total -= 2;

    int x = panelX() + (W - total) / 2;
    const int y = buttonRowY();
    for (int i = 0; i < (int)buttons_.size(); ++i) {
        Btn& b = buttons_[i];
        b.x = x;
        b.w = (int)b.text.size();
        r_.drawButton(b.x, y, b.text, focus_ == F_BUTTONS && i == button_, i == pressed_button_);
        x += b.w + 2;
    }
}

void Wizard::flashButton(int idx) {
    if (idx < 0 || idx >= (int)buttons_.size()) return;
    pressed_button_ = idx;
    drawCurrentPage();
    napms(120);
    pressed_button_ = -1;
    drawCurrentPage();
    napms(80);
}

// ---------------------------------------------------------------------------
//  Text helpers
// ---------------------------------------------------------------------------
std::vector<std::string> Wizard::wrap(const std::string& text, int width) {
    // Consecutive non-blank source lines are one paragraph and get re-filled to
    // `width`; a blank line separates paragraphs and is kept. Without the folding
    // step the wrapped output inherits wherever the yaml author happened to break
    // their lines, which reads as ragged half-empty rows.
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string src, para;

    auto flush = [&]() {
        if (para.empty()) return;
        std::string line, word;
        std::istringstream words(para);
        while (words >> word) {
            if (line.empty())                                        line = word;
            else if ((int)(line.size() + 1 + word.size()) <= width)  line += " " + word;
            else { out.push_back(line); line = word; }
        }
        if (!line.empty()) out.push_back(line);
        para.clear();
    };

    while (std::getline(in, src)) {
        if (!src.empty() && src.back() == '\r') src.pop_back();
        // An indented line is deliberate layout (the licence's component list),
        // so it stands on its own instead of being folded into the paragraph.
        const bool indented = !src.empty() && (src[0] == ' ' || src[0] == '\t');
        if (src.find_first_not_of(" \t") == std::string::npos) { flush(); out.push_back(""); }
        else if (indented)                                     { flush(); out.push_back(src); }
        else                                                   { para += (para.empty() ? "" : " ") + src; }
    }
    flush();
    return out;
}

void Wizard::drawTextBlock(const std::vector<std::string>& lines, int x, int y, int h,
                           int scroll, int color) {
    for (int i = 0; i < h; ++i) {
        const int idx = scroll + i;
        std::string text = (idx >= 0 && idx < (int)lines.size()) ? lines[idx] : std::string();
        if ((int)text.size() > contentW()) text = text.substr(0, contentW());
        text.resize(contentW(), ' ');
        r_.drawText(x, y + i, text, color);
    }
}

void Wizard::drawProgressBar(int x, int y, int w, double frac) {
    frac = std::max(0.0, std::min(1.0, frac));
    const int filled = (int)(frac * w + 0.5);
    r_.drawText(x, y, repeat(BAR_FULL, filled), Renderer::CP_STATUS_BAR_HIGHLIGHT);
    r_.drawText(x + filled, y, repeat(BAR_EMPTY, w - filled), Renderer::CP_DIALOG);

    char pct[16];
    std::snprintf(pct, sizeof pct, "%3d%%", (int)(frac * 100 + 0.5));
    r_.drawText(x + w / 2 - 2, y + 2, pct, Renderer::CP_DIALOG, A_BOLD);
}

void Wizard::drawInputField(int x, int y, int w, const std::string& text, int cursor,
                            int scroll, bool focused) {
    std::string view = text.substr(std::min((size_t)scroll, text.size()));
    if ((int)view.size() > w) view = view.substr(0, w);
    view.resize(w, ' ');
    r_.drawText(x, y, view, focused ? Renderer::CP_LIST_SELECTED : Renderer::CP_LIST_BOX);
    if (focused) {
        r_.setCursor(x + cursor - scroll, y);
        r_.showCursor();
    }
}

std::string Wizard::humanSize(uint64_t n) {
    char buf[48];
    if (n >= 1024ull * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", n / (1024.0 * 1024.0));
    else if (n >= 1024)      std::snprintf(buf, sizeof buf, "%.0f KB", n / 1024.0);
    else                     std::snprintf(buf, sizeof buf, "%llu B", (unsigned long long)n);
    return buf;
}

std::vector<std::string> Wizard::selectedIds() const {
    std::vector<std::string> ids;
    for (const auto& c : components_) if (c.selected) ids.push_back(c.id);
    return ids;
}

// ---------------------------------------------------------------------------
//  Pages
// ---------------------------------------------------------------------------
void Wizard::drawWelcome() {
    const json product = spec_.value("product", json::object());
    const json ui      = spec_.value("ui", json::object());

    const int x = contentX();
    int y = contentY();

    r_.drawText(x, y, center(product.value("name", std::string("Application")), contentW()),
                Renderer::CP_DIALOG, A_BOLD);
    y += 1;
    const std::string sub = "version " + product.value("version", std::string("?")) +
                            "   -   " + product.value("publisher", std::string());
    r_.drawText(x, y, center(sub, contentW()), Renderer::CP_DIALOG);
    y += 2;

    // Everything down to the row above the button row is fair game.
    const auto lines = wrap(ui.value("welcome", std::string()), contentW());
    const int room = panelY() + H - 4 - y;
    drawTextBlock(lines, x, y, std::min((int)lines.size(), room), 0, Renderer::CP_DIALOG);
}

void Wizard::drawLicense() {
    const int h = contentH() - 1;
    drawTextBlock(license_lines_, contentX(), contentY(), h, license_scroll_, Renderer::CP_LIST_BOX);

    char pos[64];
    const int last = std::max(0, (int)license_lines_.size() - h);
    std::snprintf(pos, sizeof pos, "line %d of %d",
                  std::min(license_scroll_ + h, (int)license_lines_.size()),
                  (int)license_lines_.size());
    r_.drawText(contentX(), contentY() + h, pos, Renderer::CP_DIALOG);
    if (license_scroll_ < last)
        r_.drawText(contentX() + contentW() - 20, contentY() + h, "PgDn for more",
                    Renderer::CP_DIALOG, A_BOLD);
}

void Wizard::drawDir() {
    const int x = contentX();
    int y = contentY();

    const std::string pname =
        spec_.value("product", json::object()).value("name", std::string("the program"));
    r_.drawText(x, y, "Install " + pname + " into this directory:", Renderer::CP_DIALOG);
    y += 2;

    const int field_w = contentW() - 2;
    if (dir_cursor_ - dir_scroll_ >= field_w) dir_scroll_ = dir_cursor_ - field_w + 1;
    if (dir_cursor_ < dir_scroll_)            dir_scroll_ = dir_cursor_;
    dir_field_y_ = y;
    drawInputField(x, y, field_w, dir_, dir_cursor_, dir_scroll_, focus_ == F_CONTENT);
    y += 2;

    const uint64_t need = inst::sizeOf(pl_, selectedIds());
    r_.drawText(x, y, "Space required: " + humanSize(need), Renderer::CP_DIALOG);
    y += 2;

    if (inst::pathExists(dir_))
        r_.drawText(x, y, "The directory exists; its contents will be updated.", Renderer::CP_DIALOG);
    else
        r_.drawText(x, y, "The directory will be created.", Renderer::CP_DIALOG);
    y += 1;
    if (!inst::isDirWritable(dir_))
        r_.drawText(x, y, "Warning: this location does not look writable.",
                    Renderer::CP_STATUS_BAR_HIGHLIGHT, A_BOLD);
}

void Wizard::drawComponents() {
    const int x = contentX();
    int y = contentY();
    r_.drawText(x, y, "Choose what to install - Space toggles, Enter continues:", Renderer::CP_DIALOG);
    y += 1;

    const int rows = contentH() - 4;
    for (int i = 0; i < rows; ++i) {
        const int idx = comp_scroll_ + i;
        std::string line(contentW(), ' ');
        if (idx < (int)components_.size()) {
            const Component& c = components_[idx];
            const std::string mark = c.required ? "[*] " : (c.selected ? "[X] " : "[ ] ");
            std::string text = mark + c.name;
            const std::string size = humanSize(c.bytes);
            if ((int)(text.size() + size.size() + 2) < contentW())
                text += std::string(contentW() - text.size() - size.size(), ' ') + size;
            line = text;
            line.resize(contentW(), ' ');
        }
        const bool sel = (focus_ == F_CONTENT) && (comp_scroll_ + i == comp_cursor_) &&
                         (comp_scroll_ + i < (int)components_.size());
        r_.drawText(x, y + i, line, sel ? Renderer::CP_LIST_SELECTED : Renderer::CP_LIST_BOX);
    }
    y += rows + 1;

    if (comp_cursor_ >= 0 && comp_cursor_ < (int)components_.size()) {
        std::string d = components_[comp_cursor_].description;
        if ((int)d.size() > contentW()) d = d.substr(0, contentW());
        d.resize(contentW(), ' ');
        r_.drawText(x, y, d, Renderer::CP_DIALOG);
    }
}

void Wizard::drawConfirm() {
    const int x = contentX();
    int y = contentY();

    r_.drawText(x, y, "Ready to install.", Renderer::CP_DIALOG, A_BOLD);
    y += 2;
    r_.drawText(x, y++, "Directory : " + dir_, Renderer::CP_DIALOG);

    std::string comps;
    for (const auto& c : components_) if (c.selected) comps += (comps.empty() ? "" : ", ") + c.name;
    for (const auto& l : wrap("Components: " + comps, contentW()))
        r_.drawText(x, y++, l, Renderer::CP_DIALOG);

    r_.drawText(x, y++, "Total     : " + humanSize(inst::sizeOf(pl_, selectedIds())),
                Renderer::CP_DIALOG);
    y += 1;

    confirm_chk_y_[0] = y;
    r_.drawText(x, y++, std::string(make_start_menu_ ? "[X]" : "[ ]") + " Add a Start Menu entry",
                focus_ == F_CONTENT && comp_cursor_ == 0 ? Renderer::CP_LIST_SELECTED
                                                         : Renderer::CP_DIALOG);
    confirm_chk_y_[1] = y;
    r_.drawText(x, y++, std::string(make_desktop_ ? "[X]" : "[ ]") + " Add a desktop shortcut",
                focus_ == F_CONTENT && comp_cursor_ == 1 ? Renderer::CP_LIST_SELECTED
                                                         : Renderer::CP_DIALOG);
}

void Wizard::drawProgress(const std::string& current, uint64_t done, uint64_t total) {
    drawDesktop();
    drawPanel(uninstall_mode_ ? "Removing" : "Installing");
    drawHints("Please wait...");

    const int x = contentX();
    int y = contentY() + 2;

    std::string what = current.empty() ? std::string("Finishing up...") : current;
    if ((int)what.size() > contentW()) what = "..." + what.substr(what.size() - contentW() + 3);
    what.resize(contentW(), ' ');
    r_.drawText(x, y, what, Renderer::CP_DIALOG);
    y += 2;

    drawProgressBar(x, y, contentW(), total ? (double)done / (double)total : 0.0);
    y += 4;
    r_.drawText(x, y, humanSize(done) + " of " + humanSize(total), Renderer::CP_DIALOG);

    r_.hideCursor();
    r_.refresh();
}

void Wizard::drawDone() {
    const json ui = spec_.value("ui", json::object());
    const int x = contentX();
    int y = contentY();

    if (!result_.ok) {
        r_.drawText(x, y, center("Installation failed", contentW()),
                    Renderer::CP_STATUS_BAR_HIGHLIGHT, A_BOLD);
        y += 2;
        for (const auto& l : wrap(result_.error, contentW())) r_.drawText(x, y++, l, Renderer::CP_DIALOG);
        return;
    }

    r_.drawText(x, y, center(uninstall_mode_ ? "Removal complete" : "Installation complete",
                             contentW()), Renderer::CP_DIALOG, A_BOLD);
    y += 2;

    if (uninstall_mode_) {
        r_.drawText(x, y++, std::to_string(result_.files_written) + " files removed.",
                    Renderer::CP_DIALOG);
        return;
    }

    for (const auto& l : wrap(ui.value("finish", std::string()), contentW()))
        r_.drawText(x, y++, l, Renderer::CP_DIALOG);
    y += 1;
    r_.drawText(x, y++, "Installed to : " + dir_, Renderer::CP_DIALOG);
    r_.drawText(x, y++, "Files        : " + std::to_string(result_.files_written) + "  (" +
                        humanSize(result_.bytes_written) + ")", Renderer::CP_DIALOG);
    y += 1;

    if (!launch_target_.empty()) {
        const std::string pname = spec_.value("product", json::object()).value("name", std::string("it"));
        done_chk_y_ = y;
        r_.drawText(x, y, std::string(launch_ ? "[X]" : "[ ]") + " Run " + pname + " now",
                    focus_ == F_CONTENT ? Renderer::CP_LIST_SELECTED : Renderer::CP_DIALOG);
    }
}

void Wizard::drawCurrentPage() {
    static const char* TITLES[P_COUNT] = {
        "Welcome", "License Agreement", "Destination Directory",
        "Select Components", "Confirm", "Installing", "Finished"
    };
    static const char* HINTS[P_COUNT] = {
        "Enter Continue   Esc Exit",
        "PgUp/PgDn Scroll   Tab Buttons   Esc Exit",
        "Tab Switch   Enter Continue   Esc Exit",
        "Space Toggle   Up/Down Move   Tab Buttons   Esc Exit",
        "Space Toggle   Tab Switch   Enter Install   Esc Exit",
        "Please wait...",
        "Enter Finish"
    };

    drawDesktop();
    drawPanel(TITLES[page_]);
    drawHints(HINTS[page_]);

    r_.hideCursor();
    switch (page_) {
        case P_WELCOME:    drawWelcome();    break;
        case P_LICENSE:    drawLicense();    break;
        case P_DIR:        drawDir();        break;
        case P_COMPONENTS: drawComponents(); break;
        case P_CONFIRM:    drawConfirm();    break;
        case P_DONE:       drawDone();       break;
        default: break;
    }
    drawButtons();
    // Only the directory field wants a visible caret; everywhere else a stray
    // block would be left sitting wherever the last drawText finished.
    if (!(page_ == P_DIR && focus_ == F_CONTENT)) r_.hideCursor();
    r_.refresh();
}

// ---------------------------------------------------------------------------
//  Flow
// ---------------------------------------------------------------------------
void Wizard::setPage(Page p) {
    page_   = p;
    button_ = 0;
    focus_  = F_BUTTONS;

    switch (p) {
        case P_WELCOME:    buttons_ = { { "  &Continue  ", B_CONTINUE }, { "  E&xit  ", B_EXIT } };
                           break;
        case P_LICENSE:    buttons_ = { { "  I &Agree  ", B_CONTINUE }, { "  &Back  ", B_BACK },
                                        { "  E&xit  ", B_EXIT } };
                           break;
        case P_DIR:        buttons_ = { { "  &Continue  ", B_CONTINUE }, { "  B&rowse  ", B_BROWSE },
                                        { "  &Back  ", B_BACK }, { "  E&xit  ", B_EXIT } };
                           focus_ = F_CONTENT; break;
        case P_COMPONENTS: buttons_ = { { "  &Continue  ", B_CONTINUE }, { "  &Back  ", B_BACK },
                                        { "  E&xit  ", B_EXIT } };
                           focus_ = F_CONTENT; comp_cursor_ = 0; break;
        case P_CONFIRM:    buttons_ = { { "  &Install  ", B_CONTINUE }, { "  &Back  ", B_BACK },
                                        { "  E&xit  ", B_EXIT } };
                           comp_cursor_ = 0; break;
        case P_PROGRESS:   buttons_.clear(); break;
        case P_DONE:       buttons_ = { { "  &Finish  ", B_FINISH } };
                           focus_ = launch_target_.empty() ? F_BUTTONS : F_CONTENT; break;
        default: break;
    }

    // A script with no license text has nothing to show on that page.
    if (p == P_LICENSE && license_lines_.empty()) setPage(P_DIR);
}

bool Wizard::canAdvance() const {
    if (page_ == P_DIR) return !dir_.empty();
    if (page_ == P_COMPONENTS)
        return std::any_of(components_.begin(), components_.end(),
                           [](const Component& c) { return c.selected; });
    return true;
}

void Wizard::advance() {
    if (!canAdvance()) {
        message("Cannot continue", page_ == P_DIR
                ? "Please give a directory to install into."
                : "Please select at least one component.");
        return;
    }
    switch (page_) {
        case P_WELCOME:    setPage(P_LICENSE);    break;
        case P_LICENSE:    setPage(P_DIR);        break;
        case P_DIR:        setPage(P_COMPONENTS); break;
        case P_COMPONENTS: setPage(P_CONFIRM);    break;
        case P_CONFIRM:    setPage(P_PROGRESS);   break;
        default: break;
    }
}

void Wizard::back() {
    switch (page_) {
        case P_LICENSE:    setPage(P_WELCOME);    break;
        case P_DIR:        setPage(license_lines_.empty() ? P_WELCOME : P_LICENSE); break;
        case P_COMPONENTS: setPage(P_DIR);        break;
        case P_CONFIRM:    setPage(P_COMPONENTS); break;
        default: break;
    }
}

void Wizard::message(const std::string& title, const std::string& text) {
    const auto lines = wrap(text, 44);
    const int w = 52, h = (int)lines.size() + 6;
    const int x = (r_.getWidth() - w) / 2, y = (r_.getHeight() - h) / 2;

    const int ok_x = x + (w - 10) / 2, ok_y = y + h - 3;
    const std::string ok_text = "  &OK  ";

    auto paint = [&](bool pressed) {
        r_.drawShadow(x, y, w, h);
        r_.drawBoxWithTitle(x, y, w, h, Renderer::CP_DIALOG, Renderer::DOUBLE,
                            " " + title + " ", Renderer::CP_DIALOG_TITLE, A_BOLD);
        for (int i = 1; i < h - 1; ++i)
            r_.drawText(x + 1, y + i, std::string(w - 2, ' '), Renderer::CP_DIALOG);
        for (int i = 0; i < (int)lines.size(); ++i)
            r_.drawText(x + 4, y + 2 + i, lines[i], Renderer::CP_DIALOG);
        r_.drawButton(ok_x, ok_y, ok_text, true, pressed);
        r_.hideCursor();
        r_.refresh();
    };
    auto flash = [&]() { paint(true); napms(120); paint(false); napms(80); };

    for (;;) {
        paint(false);

        bool alt = false;
        const wint_t ch = nextKey(alt);
        if (ch == (wint_t)ERR) { napms(16); continue; }
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) != OK) continue;
            if ((ev.bstate & REPORT_MOUSE_POSITION) ||
                !(ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))) continue;
            if (ev.y == ok_y && ev.x >= ok_x && ev.x < ok_x + (int)ok_text.size()) { flash(); return; }
            continue;
        }
        if (ch == 27 || ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == ' ' ||
            ch == 'o' || ch == 'O') {
            flash();
            return;
        }
    }
}

bool Wizard::confirmQuit() {
    const int w = 52, h = 9;
    const int x = (r_.getWidth() - w) / 2, y = (r_.getHeight() - h) / 2;
    int sel = 1;   // default to "No"

    const int by = y + h - 3, yes_x = x + 12, no_x = x + 28;
    const std::string yes_text = "  &Yes  ", no_text = "  &No  ";

    // pressed: -1 none, 0 Yes, 1 No
    auto paint = [&](int pressed) {
        r_.drawShadow(x, y, w, h);
        r_.drawBoxWithTitle(x, y, w, h, Renderer::CP_DIALOG, Renderer::DOUBLE,
                            " Exit Setup ", Renderer::CP_DIALOG_TITLE, A_BOLD);
        for (int i = 1; i < h - 1; ++i)
            r_.drawText(x + 1, y + i, std::string(w - 2, ' '), Renderer::CP_DIALOG);
        r_.drawText(x + 4, y + 2, "Installation is not finished.", Renderer::CP_DIALOG);
        r_.drawText(x + 4, y + 3, "Quit setup now?", Renderer::CP_DIALOG);
        r_.drawButton(yes_x, by, yes_text, sel == 0, pressed == 0);
        r_.drawButton(no_x,  by, no_text,  sel == 1, pressed == 1);
        r_.hideCursor();
        r_.refresh();
    };
    auto answer = [&](int which) {
        paint(which); napms(120); paint(-1); napms(80);
        return which == 0;
    };

    for (;;) {
        paint(-1);

        bool alt = false;
        const wint_t ch = nextKey(alt);
        if (ch == (wint_t)ERR) { napms(16); continue; }
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) != OK) continue;
            if ((ev.bstate & REPORT_MOUSE_POSITION) ||
                !(ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))) continue;
            if (ev.y == by && ev.x >= yes_x && ev.x < yes_x + (int)yes_text.size()) return answer(0);
            if (ev.y == by && ev.x >= no_x  && ev.x < no_x  + (int)no_text.size())  return answer(1);
            continue;
        }
        if (ch == KEY_LEFT  || ch == 9) sel = (sel + 1) % 2;
        if (ch == KEY_RIGHT)            sel = (sel + 1) % 2;
        if (ch == 'y' || ch == 'Y') return answer(0);
        if (ch == 'n' || ch == 'N') return answer(1);
        if (ch == 27) return false;
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) return answer(sel);
    }
}

// Perform a button's action. Returns false when the wizard should stop looping.
bool Wizard::activate(ButtonId id) {
    switch (id) {
        case B_EXIT:
        case B_CANCEL:
            return !confirmQuit();
        case B_BACK:
            back();
            return true;
        case B_BROWSE: {
            const std::string picked = FileBrowser::selectDirectory(r_);
            if (!picked.empty()) { dir_ = inst::nativePath(picked); dir_cursor_ = (int)dir_.size(); }
            return true;
        }
        case B_FINISH:
            return false;
        case B_CONTINUE:
        case B_REMOVE:
            advance();
            return true;
    }
    return true;
}

wint_t Wizard::nextKey(bool& alt) {
    alt = false;
    const wint_t ch = r_.getChar();
    if (ch != 27) return ch;

    // Peek: a lone Escape is followed by nothing, Alt+<key> by the key itself,
    // and a terminal-style escape sequence by '['.
    timeout(50);
    const wint_t next = r_.getChar();
    nodelay(stdscr, TRUE);

    if (next == (wint_t)ERR) return 27;              // a real Escape

    if (next == '[') {                               // CSI - consume and ignore
        timeout(30);
        wint_t c;
        for (int n = 0; n < 16 && (c = r_.getChar()) != (wint_t)ERR; ++n)
            if (c >= 64 && c <= 126) break;
        nodelay(stdscr, TRUE);
        return (wint_t)ERR;
    }

    alt = true;
    return next;
}

bool Wizard::handleKey(wint_t ch, bool alt) {
    // Button hotkeys: always with Alt, and without it too except while a text
    // field has focus - otherwise typing "c" into the install path would fire
    // Continue instead of reaching the field.
    if (ch < 128 && std::isalpha((int)ch) && (alt || !textFieldFocused())) {
        for (int i = 0; i < (int)buttons_.size(); ++i) {
            const size_t amp = buttons_[i].text.find('&');
            if (amp == std::string::npos || amp + 1 >= buttons_[i].text.size()) continue;
            if (std::tolower((unsigned char)buttons_[i].text[amp + 1]) == std::tolower((int)ch)) {
                button_ = i;
                focus_  = F_BUTTONS;
                flashButton(i);
                return activate(buttons_[i].id);
            }
        }
    }
    // An Alt combination that matches nothing does nothing - it must never fall
    // through to the plain-key handling below.
    if (alt) return true;

    if (ch == 27) {                                   // Esc
        if (page_ == P_DONE) return false;
        if (confirmQuit()) return false;
        return true;
    }

    if (ch == 9 || ch == KEY_BTAB) {                  // Tab: content <-> buttons
        const bool has_content = (page_ == P_DIR || page_ == P_COMPONENTS ||
                                  page_ == P_CONFIRM || page_ == P_LICENSE ||
                                  (page_ == P_DONE && !launch_target_.empty()));
        if (has_content) focus_ = (focus_ == F_BUTTONS) ? F_CONTENT : F_BUTTONS;
        return true;
    }

    // The licence text scrolls whichever half of the page has focus. It is the
    // only scrollable thing there, focus starts on the buttons so Enter means
    // "I Agree", and the hint bar promises PgUp/PgDn outright - so these keys
    // must not fall into the button branch below, which swallows what it does
    // not use. Left/Right are left alone; they still move between buttons.
    if (page_ == P_LICENSE) {
        const int h = contentH() - 1;
        const int last = std::max(0, (int)license_lines_.size() - h);
        switch (ch) {
            case KEY_UP:    license_scroll_ = std::max(0, license_scroll_ - 1);      return true;
            case KEY_DOWN:  license_scroll_ = std::min(last, license_scroll_ + 1);   return true;
            case KEY_PPAGE: license_scroll_ = std::max(0, license_scroll_ - h);      return true;
            case KEY_NPAGE: license_scroll_ = std::min(last, license_scroll_ + h);   return true;
            case KEY_HOME:  license_scroll_ = 0;                                     return true;
            case KEY_END:   license_scroll_ = last;                                  return true;
            default: break;
        }
    }

    if (focus_ == F_BUTTONS) {
        if (ch == KEY_LEFT)  { if (button_ > 0) --button_; return true; }
        if (ch == KEY_RIGHT) { if (button_ + 1 < (int)buttons_.size()) ++button_; return true; }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == ' ') {
            if (button_ >= 0 && button_ < (int)buttons_.size()) {
                flashButton(button_);
                return activate(buttons_[button_].id);
            }
        }
        return true;
    }

    // ---- content focus ----
    switch (page_) {
        // P_LICENSE is handled above, before the focus split.
        case P_DIR: {
            if (ch == KEY_LEFT)  { if (dir_cursor_ > 0) --dir_cursor_; }
            else if (ch == KEY_RIGHT) { if (dir_cursor_ < (int)dir_.size()) ++dir_cursor_; }
            else if (ch == KEY_HOME)  dir_cursor_ = 0;
            else if (ch == KEY_END)   dir_cursor_ = (int)dir_.size();
            else if (ch == KEY_BACKSPACE || ch == 8 || ch == 127) {
                if (dir_cursor_ > 0) { dir_.erase(dir_cursor_ - 1, 1); --dir_cursor_; }
            } else if (ch == KEY_DC) {
                if (dir_cursor_ < (int)dir_.size()) dir_.erase(dir_cursor_, 1);
            } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                advance();
            } else if (ch >= 32 && ch < 127) {
                dir_.insert(dir_.begin() + dir_cursor_, (char)ch);
                ++dir_cursor_;
            }
            break;
        }
        case P_COMPONENTS: {
            const int rows = contentH() - 4;
            if (ch == KEY_UP   && comp_cursor_ > 0) --comp_cursor_;
            if (ch == KEY_DOWN && comp_cursor_ + 1 < (int)components_.size()) ++comp_cursor_;
            if (ch == ' ') {
                Component& c = components_[comp_cursor_];
                if (!c.required) c.selected = !c.selected;
            }
            if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) advance();
            if (comp_cursor_ < comp_scroll_)              comp_scroll_ = comp_cursor_;
            if (comp_cursor_ >= comp_scroll_ + rows)      comp_scroll_ = comp_cursor_ - rows + 1;
            break;
        }
        case P_CONFIRM: {
            if (ch == KEY_UP   && comp_cursor_ > 0) --comp_cursor_;
            if (ch == KEY_DOWN && comp_cursor_ < 1) ++comp_cursor_;
            if (ch == ' ') {
                if (comp_cursor_ == 0) make_start_menu_ = !make_start_menu_;
                else                   make_desktop_    = !make_desktop_;
            }
            if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) advance();
            break;
        }
        case P_DONE: {
            if (ch == ' ') launch_ = !launch_;
            if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) return false;
            break;
        }
        default: break;
    }
    return true;
}

bool Wizard::handleMouse(const MEVENT& ev) {
    // This backend reports pointer motion as BUTTON1_PRESSED|REPORT_MOUSE_POSITION,
    // so without excluding position reports a plain hover would fire a button.
    const bool is_pos_rpt = (ev.bstate & REPORT_MOUSE_POSITION) != 0;
    const bool is_press   = (ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) != 0 && !is_pos_rpt;
    const bool wheel_up   = (ev.bstate & BUTTON4_PRESSED) != 0;
    const bool wheel_down = (ev.bstate & BUTTON5_PRESSED) != 0;

    // The licence page scrolls under the wheel wherever the pointer happens to be.
    if ((wheel_up || wheel_down) && page_ == P_LICENSE) {
        const int h = contentH() - 1;
        const int last = std::max(0, (int)license_lines_.size() - h);
        license_scroll_ = std::max(0, std::min(last, license_scroll_ + (wheel_up ? -3 : 3)));
        return true;
    }
    if (wheel_up || wheel_down) {
        if (page_ == P_COMPONENTS) {
            const int rows = contentH() - 4;
            comp_cursor_ = std::max(0, std::min((int)components_.size() - 1,
                                                comp_cursor_ + (wheel_up ? -1 : 1)));
            if (comp_cursor_ < comp_scroll_)         comp_scroll_ = comp_cursor_;
            if (comp_cursor_ >= comp_scroll_ + rows) comp_scroll_ = comp_cursor_ - rows + 1;
        }
        return true;
    }
    if (!is_press) return true;

    // ---- the button row ----------------------------------------------------
    if (ev.y == buttonRowY()) {
        for (int i = 0; i < (int)buttons_.size(); ++i) {
            const Btn& b = buttons_[i];
            if (ev.x >= b.x && ev.x < b.x + b.w) {
                button_ = i;
                focus_  = F_BUTTONS;
                flashButton(i);             // push it in before acting
                return activate(b.id);
            }
        }
        return true;
    }

    // ---- page content ------------------------------------------------------
    switch (page_) {
        case P_DIR:
            if (ev.y == dir_field_y_ && ev.x >= contentX() && ev.x < contentX() + contentW() - 2) {
                focus_      = F_CONTENT;
                dir_cursor_ = std::min((int)dir_.size(), dir_scroll_ + (ev.x - contentX()));
            }
            break;

        case P_COMPONENTS: {
            const int top = contentY() + 1;
            const int rows = contentH() - 4;
            if (ev.y >= top && ev.y < top + rows &&
                ev.x >= contentX() && ev.x < contentX() + contentW()) {
                const int idx = comp_scroll_ + (ev.y - top);
                if (idx >= 0 && idx < (int)components_.size()) {
                    focus_       = F_CONTENT;
                    comp_cursor_ = idx;
                    // Clicking the "[X]" marker toggles; clicking the name selects.
                    if (ev.x < contentX() + 3 && !components_[idx].required)
                        components_[idx].selected = !components_[idx].selected;
                }
            }
            break;
        }

        case P_CONFIRM:
            for (int i = 0; i < 2; ++i) {
                if (ev.y == confirm_chk_y_[i] && ev.x >= contentX() &&
                    ev.x < contentX() + contentW()) {
                    focus_       = F_CONTENT;
                    comp_cursor_ = i;
                    if (ev.x < contentX() + 3) {
                        if (i == 0) make_start_menu_ = !make_start_menu_;
                        else        make_desktop_    = !make_desktop_;
                    }
                }
            }
            break;

        case P_DONE:
            if (!launch_target_.empty() && ev.y == done_chk_y_ &&
                ev.x >= contentX() && ev.x < contentX() + contentW()) {
                focus_ = F_CONTENT;
                if (ev.x < contentX() + 3) launch_ = !launch_;
            }
            break;

        default: break;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Main loops
// ---------------------------------------------------------------------------
bool Wizard::run() {
    for (;;) {
        if (page_ == P_PROGRESS) {
            inst::Plan plan;
            plan.dir                = dir_;
            plan.components         = selectedIds();
            plan.start_menu         = make_start_menu_;
            plan.desktop            = make_desktop_;
            plan.register_uninstall = spec_.value("install", json::object())
                                            .value("register_uninstall", true);

            result_ = inst::install(pl_, plan, [&](const std::string& what, uint64_t d, uint64_t t) {
                drawProgress(what, d, t);
                r_.getChar();          // keep the window responsive while unpacking
            });
            installed_ = result_.ok;
            setPage(P_DONE);
            continue;
        }

        drawCurrentPage();

        bool alt = false;
        const wint_t ch = nextKey(alt);
        if (ch == (wint_t)ERR) { napms(16); continue; }
        if (ch == KEY_RESIZE)  { r_.updateDimensions(); continue; }
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK && !handleMouse(ev)) break;
            continue;
        }
        if (!handleKey(ch, alt)) break;
    }
    return installed_;
}

bool Wizard::runUninstall(const json& manifest) {
    uninstall_mode_ = true;
    const std::string name = manifest.value("product", json::object())
                                     .value("name", std::string("this program"));

    // ---- confirm -----------------------------------------------------------
    for (;;) {
        drawDesktop();
        drawPanel("Uninstall");
        drawHints("Enter Remove   Esc Cancel");
        const int x = contentX();
        int y = contentY() + 1;
        r_.drawText(x, y++, "Remove " + name + " from this computer?", Renderer::CP_DIALOG, A_BOLD);
        y += 1;
        r_.drawText(x, y++, "Directory: " + manifest.value("dir", std::string()), Renderer::CP_DIALOG);
        r_.drawText(x, y++, "Files    : " +
                    std::to_string(manifest.value("files", json::array()).size()), Renderer::CP_DIALOG);

        buttons_ = { { "  &Remove  ", B_REMOVE }, { "  &Cancel  ", B_CANCEL } };
        drawButtons();
        r_.hideCursor();
        r_.refresh();

        bool alt = false;
        const wint_t ch = nextKey(alt);
        if (ch == (wint_t)ERR) { napms(16); continue; }
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) != OK) continue;
            if ((ev.bstate & REPORT_MOUSE_POSITION) ||
                !(ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))) continue;
            if (ev.y != buttonRowY()) continue;
            bool go = false, cancel = false;
            for (int i = 0; i < (int)buttons_.size(); ++i)
                if (ev.x >= buttons_[i].x && ev.x < buttons_[i].x + buttons_[i].w) {
                    if (buttons_[i].id == B_REMOVE) { button_ = i; go = true; }
                    else                            cancel = true;
                }
            if (cancel) return false;
            if (go)     break;
            continue;
        }
        if (ch == 27 || ch == 'c' || ch == 'C') return false;
        if (ch == KEY_LEFT)  { if (button_ > 0) --button_; continue; }
        if (ch == KEY_RIGHT) { if (button_ + 1 < (int)buttons_.size()) ++button_; continue; }
        if (ch == 'r' || ch == 'R') break;
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) { if (button_ == 0) break; return false; }
    }

    // ---- remove ------------------------------------------------------------
    result_ = inst::uninstall(manifest, [&](const std::string& what, uint64_t d, uint64_t t) {
        drawProgress(what, d, t);
        r_.getChar();
    });

    // ---- report ------------------------------------------------------------
    setPage(P_DONE);
    for (;;) {
        drawCurrentPage();
        bool alt = false;
        const wint_t ch = nextKey(alt);
        if (ch == (wint_t)ERR) { napms(16); continue; }
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK && !handleMouse(ev)) break;
            continue;
        }
        if (ch == 27 || ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == 'f' || ch == 'F') break;
    }
    return result_.ok;
}
