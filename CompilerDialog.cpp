#include "CompilerDialog.h"
#include "BuildSystem.h"
#include "DependencyChecker.h"
#include "FileBrowser.h"
#include "MessageDialog.h"
#include "curses_compat.h"
#include "platform_compat.h"
#include "utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  LineField
// ─────────────────────────────────────────────────────────────────────────────

bool LineField::key(wint_t ch)
{
    if (ch == KEY_LEFT)       { if (cursor > 0) --cursor; return false; }
    if (ch == KEY_RIGHT)      { if (cursor < (int)text.size()) ++cursor; return false; }
    if (ch == KEY_HOME)       { cursor = 0; return false; }
    if (ch == KEY_END)        { cursor = (int)text.size(); return false; }
    if (ch == KEY_BACKSPACE || ch == 8 || ch == 127) {
        if (cursor == 0) return false;
        text.erase(--cursor, 1);
        return true;
    }
    if (ch == KEY_DC) {
        if (cursor >= (int)text.size()) return false;
        text.erase(cursor, 1);
        return true;
    }
    if (ch > 31 && ch < KEY_MIN) {
        text.insert(text.begin() + cursor, (char)ch);
        ++cursor;
        return true;
    }
    return false;
}

void LineField::draw(Renderer& r, int x, int y, int w)
{
    if (cursor - scroll >= w) scroll = cursor - w + 1;
    if (cursor < scroll)      scroll = cursor;
    r.drawText(x, y, std::string(w, ' '), Renderer::CP_LIST_BOX);
    if ((int)text.size() > scroll)
        r.drawText(x, y, text.substr(scroll, w), Renderer::CP_LIST_BOX);
}

int LineField::cursorX(int x, int w)
{
    if (cursor - scroll >= w) scroll = cursor - w + 1;
    if (cursor < scroll)      scroll = cursor;
    return x + (cursor - scroll);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Shared helpers
// ─────────────────────────────────────────────────────────────────────────────

// Which of the installer's environment set-ups a definition asks for.
static toolchain::Kind setupKind(const std::string& setup)
{
    if (setup == "gcc")    return toolchain::Kind::Gcc;
    if (setup == "clang")  return toolchain::Kind::Clang;
    if (setup == "msvc")   return toolchain::Kind::Msvc;
    if (setup == "watcom") return toolchain::Kind::Watcom;
    return toolchain::Kind::None;
}

// File-dialog patterns for a definition's executables ("g++;g++-*;...").
static std::string browsePatternFor(const compilers::Definition& d)
{
    std::string p;
    for (const auto& e : d.executables) {
        if (!p.empty()) p += ';';
#ifdef _WIN32
        p += e + ".exe";
#else
        p += e;
#endif
    }
    return p.empty() ? std::string("*") : p;
}

// Open a file dialog for a compiler executable. FileBrowser navigates by chdir
// and leaves us wherever the user stopped; the editor resolves relative paths
// against its own working directory, so it has to come back.
static std::string pickExecutable(Renderer& renderer, const std::string& title,
                                  const std::vector<FilterEntry>& filters,
                                  const std::string& start)
{
    char saved[1024];
    const bool have_saved = getcwd(saved, sizeof saved) != nullptr;
    const std::string picked = FileBrowser::open(renderer, title, filters, start);
    if (have_saved) { const int rc = chdir(saved); (void)rc; }
    return picked;
}

static std::string startDirFor(const std::string& current)
{
    std::error_code ec;
    if (!current.empty()) {
        const fs::path p(current);
        if (fs::is_directory(p, ec))               return p.string();
        if (fs::is_directory(p.parent_path(), ec)) return p.parent_path().string();
    }
#ifdef _WIN32
    return "C:\\";
#else
    return "/usr/bin";
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  CompilerDialog
// ─────────────────────────────────────────────────────────────────────────────

CompilerDialog::CompilerDialog(Renderer& renderer, BuildSystem& buildSystem, Config& config)
    : DialogBase("Compiler Options", W, height(descRowsNeeded()))
    , renderer_   (renderer)
    , buildSystem_(buildSystem)
    , config_     (config)
    , desc_rows_  (descRowsNeeded())
{
    const Toolchain& tc = config_.toolchain;
    const compilers::Definition& cur =
        compilers::Registry::instance().forToolchain(tc.compiler, tc.kind, tc.cxx);
    current_name_ = cur.name;
    paths_[cur.id] = tc.cxx;
    refreshDefinitions(cur.id);
}

// A description broken into lines of the description column's width: at
// spaces, or inside a word too long for a line.
std::vector<std::string> CompilerDialog::wrapDescription(const std::string& text)
{
    std::vector<std::string> lines;
    for (std::string l : wrap_text(text, DESC_W)) {
        if (l.empty() && lines.empty()) continue;
        while ((int)l.size() > DESC_W) { lines.push_back(l.substr(0, DESC_W)); l.erase(0, DESC_W); }
        lines.push_back(l);
    }
    return lines;
}

int CompilerDialog::descRowsNeeded()
{
    int rows = 1;
    for (const compilers::Definition* d : compilers::Registry::instance().selectable())
        rows = std::max(rows, (int)wrapDescription(d->description).size());
    return std::min(rows, DESC_MAX);
}

void CompilerDialog::show(Renderer& renderer, BuildSystem& buildSystem, Config& config)
{
    CompilerDialog dlg(renderer, buildSystem, config);
    dlg.run(renderer);
}

// (Re)read the selectable compilers - the registry is rebuilt when one is
// introduced, which moves every definition - and select `select_id`.
void CompilerDialog::refreshDefinitions(const std::string& select_id)
{
    if (!defs_.empty()) paths_[selected().id] = path_.text;

    defs_ = compilers::Registry::instance().selectable();
    names_.clear();
    sel_ = 0;
    int width = 20;
    for (int i = 0; i < (int)defs_.size(); ++i) {
        names_.push_back(defs_[i]->name);
        width = std::max(width, (int)defs_[i]->name.size() + 6);
        if (defs_[i]->id == select_id) sel_ = i;
    }
    width = std::min(width, W - FIELD_X - 4);
    combo_ = ComboBox(names_, sel_, FIELD_X, KIND_Y, width);

    // The path for the selection: what was last left there, or else that
    // compiler as found on PATH - never another compiler's path.
    const compilers::Definition& d = selected();
    std::string& p = paths_[d.id];
    if (p.empty() && pathUsable()) p = compilers::Registry::instance().findOnPath(d);
    path_.set(p);
}

bool CompilerDialog::pathUsable() const
{
    return setupKind(selected().setup) != toolchain::Kind::Msvc;
}

void CompilerDialog::select(int i)
{
    if (i == sel_ || i < 0 || i >= (int)defs_.size()) return;
    const std::string id = defs_[i]->id;
    refreshDefinitions(id);
    info_.clear();
    dirty_ = changed_ = true;
}

void CompilerDialog::probe()
{
    pending_ = {};
    error_.clear();
    warning_.clear();
    const compilers::Definition& d = selected();
    const toolchain::Kind k = setupKind(d.setup);

    // Visual Studio locates itself; the rest are built from the path given, and
    // an empty one is "not answered yet" rather than something to complain about.
    if (k == toolchain::Kind::Msvc)       pending_ = toolchain::detectMsvc(error_);
    else if (path_.text.empty())          return;
    else if (k == toolchain::Kind::None)  pending_ = toolchain::fromPathPlain(path_.text, error_);
    else                                  pending_ = toolchain::fromPath(k, path_.text, error_);
    if (!pending_.ok()) return;

    pending_.definition = d.id;
    // A different definition fitting better is worth saying - a cross compiler
    // picked as plain GCC would otherwise have its programs "run" here.
    const compilers::Definition* ident = compilers::Registry::instance().identify(pending_.cxx);
    if (ident && ident->id != d.id)
        warning_ = "This looks like " + ident->name + " rather than " + d.name + ".";
}

void CompilerDialog::onInit()
{
    {
        FocusGroup g;
        g.title = ""; g.hotkey = '\0';
        g.box_w = 0; g.box_h = 0; g.box_x = 0; g.box_y = 0;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    current_desc_ = toolchain::versionOf(config_.toolchain.cxx);

    addButtons(ButtonRow{
        .buttons = {
            Button{
                .label = " &Ok ",
                .x = W/2 - 17, .y = btnY(),
                .on_activate = [this]() -> HandleResult {
                    if (changed_) {
                        if (dirty_) { probe(); dirty_ = false; }
                        if (!pending_.ok()) {
                            MessageDialog::show(renderer_,
                                error_.empty() ? std::string("No compiler was selected.") : error_);
                            return HandleResult::CONTINUE;
                        }
                        if (!apply()) return HandleResult::CONTINUE;
                    }
                    result().accept();
                    return HandleResult::CLOSE;
                }
            },
            Button{
                .label = " &New... ",
                .x = W/2 - 7, .y = btnY(),
                .on_activate = [this]() -> HandleResult {
                    introduce();
                    return HandleResult::CONTINUE;
                }
            },
            Button{
                .label = " &Cancel ",
                .x = W/2 + 5, .y = btnY(),
                .on_activate = [this]() -> HandleResult {
                    result().cancel();
                    return HandleResult::CLOSE;
                }
            },
        }
    });

    setGroupFocus(GRP_COMPILER);
    groups()[GRP_COMPILER].inner_focus = INNER_KIND;
}

bool CompilerDialog::apply()
{
    std::string err;
    // Keep the python already recorded - write() merges, so nothing else in the
    // file (pkg-config, the --ignore-dependencies opt-out) is disturbed either.
    if (!toolchain::write(pending_, config_.toolchain.python3, err)) {
        MessageDialog::show(renderer_, "The compiler could not be saved:\n" + err);
        return false;
    }

    const compilers::Definition& d = selected();
    const bool is_clang = (d.setup == "clang");
    Toolchain& tc = config_.toolchain;
    tc.compiler   = d.id;
    tc.kind       = pending_.kind == toolchain::Kind::None ? std::string() : toolchain::id(pending_.kind);
    tc.cc         = pending_.cc;
    tc.cxx        = pending_.cxx;
    tc.clang      = is_clang ? pending_.cc  : std::string();
    tc.clang_cxx  = is_clang ? pending_.cxx : std::string();
    tc.bin_dirs   = pending_.bin_dirs;
    tc.env        = pending_.env;

    // Make this process - and everything it spawns from now on - use it.
    DependencyChecker::applyToolchainEnv(tc);
    buildSystem_.setConfig(config_);
    buildSystem_.toolchainChanged();

    current_desc_ = toolchain::versionOf(tc.cxx);
    current_name_ = d.name;
    changed_      = false;
    return true;
}

void CompilerDialog::browse()
{
    if (!pathUsable()) return;          // MSVC finds itself; nothing to browse for

    const compilers::Definition& d = selected();
    std::vector<FilterEntry> filters = {
        { d.name + " executables", browsePatternFor(d) },
#ifdef _WIN32
        { "Programs (*.exe)", "*.exe" },
#endif
        { "All files (*)", "*" },
    };
    const std::string picked = pickExecutable(renderer_, "Locate the compiler", filters,
                                              startDirFor(path_.text));
    if (picked.empty()) return;         // cancelled - leave the field alone

    // Whatever was picked, find out what it is: a known compiler switches the
    // selection to it; an unknown one is pointed at New...
    const compilers::Definition* ident = compilers::Registry::instance().identify(picked);
    if (ident && ident->id != d.id) {
        paths_[ident->id] = picked;
        refreshDefinitions(ident->id);
        info_ = "Recognised as " + ident->name + ".";
    } else {
        path_.set(picked);
        info_ = ident ? "" : "gedi does not recognise this compiler - New... introduces it.";
    }
    dirty_ = changed_ = true;
}

void CompilerDialog::pressBrowse()
{
    setGroupFocus(GRP_COMPILER);
    groups()[GRP_COMPILER].inner_focus = INNER_BROWSE;
    armCustomPress([this]() { browse(); return HandleResult::CONTINUE; });
}

void CompilerDialog::introduce()
{
    std::string exe = path_.text;
    const std::string id = NewCompilerDialog::show(renderer_, exe);
    if (id.empty()) return;
    paths_[id] = exe;
    refreshDefinitions(id);
    info_ = "Introduced " + selected().name + ". Its definition: " +
            selected().source.string();
    dirty_ = changed_ = true;
}

void CompilerDialog::onDraw(Renderer& renderer, int sx, int sy)
{
    // The combo may have been changed by a click in its dropdown.
    if (combo_.selected_idx != sel_) select(combo_.selected_idx);
    if (dirty_) { probe(); dirty_ = false; }

    const bool grp_focused = (getFocusedGroup() == GRP_COMPILER);
    const int  inner       = groups()[GRP_COMPILER].inner_focus;
    const int  textw       = W - 6;

    auto line = [&](int row, const std::string& s, int color, int style = 0) {
        std::string t = s;
        if ((int)t.size() > textw) t = t.substr(0, textw - 3) + "...";
        t.resize(textw, ' ');
        renderer.drawText(sx + 3, sy + row, t, color, style);
    };

    //  What is in use now
    std::string cur = "In use: ";
    // "Open Watcom - Open Watcom 2.0 beta" says it twice; the banner alone will do.
    if (!current_desc_.empty() && current_desc_.rfind(current_name_, 0) == 0) cur += current_desc_;
    else cur += current_name_ + (current_desc_.empty() ? "" : "  \xe2\x80\x94  " + current_desc_);
    line(CUR_Y, cur, Renderer::CP_DIALOG, A_BOLD);
    line(CURP_Y, config_.toolchain.cxx.empty() ? std::string("        (no compiler recorded)")
                                               : "        " + config_.toolchain.cxx,
         Renderer::CP_DIALOG);

    //  Pick one
    renderer.drawText(sx + 3, sy + KIND_Y, "Compiler:", Renderer::CP_DIALOG);
    combo_.draw(renderer, sx, sy, grp_focused && inner == INNER_KIND);
    // The selection's description, whole, in the rows set aside for it.
    const std::vector<std::string> desc = wrapDescription(selected().description);
    for (int i = 0; i < desc_rows_; ++i) {
        std::string t = i < (int)desc.size() ? desc[i] : std::string();
        t.resize(DESC_W, ' ');
        renderer.drawText(sx + FIELD_X, sy + DESC_Y + i, t, Renderer::CP_DIALOG, A_DIM);
    }

    //  Where it is
    if (pathUsable()) {
        renderer.drawText(sx + 3, sy + pathY(), "Path:", Renderer::CP_DIALOG);
        path_.draw(renderer, sx + FIELD_X, sy + pathY(), FIELD_W);
        renderer.drawButton(sx + BROWSE_X, sy + pathY(), " &Browse ",
                            grp_focused && inner == INNER_BROWSE, customPressed());
    } else {
        line(pathY(), "Located automatically.", Renderer::CP_DIALOG);
    }

    //  What the pick resolved to: one line - the problem if there is one, else
    //  what Browse found, else the compiler's banner. Unchanged, it is the
    //  compiler already named above.
    std::string note;
    int color = Renderer::CP_DIALOG;
    if (!error_.empty())        { note = error_;   color = Renderer::CP_COMPILE_ERROR; }
    else if (!warning_.empty()) { note = warning_; color = Renderer::CP_COMPILE_ERROR; }
    else if (!info_.empty())    { note = info_; }
    else if (changed_ && pending_.ok() && !pending_.notes.empty()) note = pending_.notes.front();
    line(noteY(), note, color);
}

bool CompilerDialog::onPlaceCursor(Renderer& renderer, int sx, int sy)
{
    if (getFocusedGroup() == GRP_COMPILER &&
        groups()[GRP_COMPILER].inner_focus == INNER_PATH && pathUsable() && !combo_.dropdown_open) {
        renderer.showCursor();
        move(sy + pathY(), path_.cursorX(sx + FIELD_X, FIELD_W));
        return true;
    }
    renderer.hideCursor();
    return true;
}

HandleResult CompilerDialog::onKey(wint_t ch)
{
    if (getFocusedGroup() != GRP_COMPILER) return HandleResult::CONTINUE;
    auto& g = groups()[GRP_COMPILER];

    // An open dropdown takes every key until it closes.
    if (combo_.dropdown_open) {
        combo_.handleKey(ch);
        if (combo_.selected_idx != sel_) select(combo_.selected_idx);
        return HandleResult::CONTINUE;
    }

    // Up/Down walk the controls: compiler, path field, Browse.
    if (ch == KEY_UP || ch == KEY_DOWN) {
        const int last = pathUsable() ? INNER_BROWSE : INNER_KIND;
        if (ch == KEY_DOWN && g.inner_focus < last) ++g.inner_focus;
        if (ch == KEY_UP   && g.inner_focus > INNER_KIND) --g.inner_focus;
        return HandleResult::CONTINUE;
    }

    if (g.inner_focus == INNER_KIND) {
        if (ch == KEY_ENTER || ch == 10 || ch == 13) combo_.open();
        else combo_.handleKey(ch);              // Left/Right/Space cycle, Ctrl+Down opens
        if (combo_.selected_idx != sel_) select(combo_.selected_idx);
        return HandleResult::CONTINUE;
    }

    if (g.inner_focus == INNER_BROWSE) {
        if (ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13) pressBrowse();
        return HandleResult::CONTINUE;
    }

    if (g.inner_focus == INNER_PATH && pathUsable()) {
        if (ch == KEY_ENTER || ch == 10 || ch == 13) { activateButtonByIndex(0); return HandleResult::CONTINUE; }
        if (path_.key(ch)) { dirty_ = changed_ = true; info_.clear(); }
    }
    return HandleResult::CONTINUE;
}

//  Tab order: compiler, path field, Browse, Ok, New, Cancel. The path field
//  and Browse are skipped for a compiler that locates itself.
bool CompilerDialog::onTab(bool forward)
{
    if (combo_.dropdown_open) combo_.close();

    std::vector<std::pair<int, int>> stops;     // {group (groupCount() = buttons), inner}
    stops.push_back({ GRP_COMPILER, INNER_KIND });
    if (pathUsable()) {
        stops.push_back({ GRP_COMPILER, INNER_PATH });
        stops.push_back({ GRP_COMPILER, INNER_BROWSE });
    }
    for (int b = 0; b < BTN_COUNT; ++b) stops.push_back({ groupCount(), b });

    const std::pair<int, int> cur = inButtonRow()
        ? std::make_pair(groupCount(), getBtnInnerFocus())
        : std::make_pair(GRP_COMPILER, groups()[GRP_COMPILER].inner_focus);

    int idx = 0;
    for (int i = 0; i < (int)stops.size(); ++i)
        if (stops[i] == cur) { idx = i; break; }
    const int n = (int)stops.size();
    idx = forward ? (idx + 1) % n : (idx + n - 1) % n;

    setGroupFocus(stops[idx].first);
    if (stops[idx].first == groupCount()) setGroupBtnFocus(stops[idx].second);
    else groups()[GRP_COMPILER].inner_focus = stops[idx].second;
    return true;
}

bool CompilerDialog::onAltKey(char lower)
{
    if (lower == 'b' && pathUsable()) { pressBrowse(); return true; }
    return false;
}

bool CompilerDialog::onMouseClick(const MEVENT& ev, int startx, int starty)
{
    // The combo first: its open dropdown floats over the rows below.
    if (combo_.handleMouse(startx, starty, ev.x, ev.y)) {
        setGroupFocus(GRP_COMPILER);
        groups()[GRP_COMPILER].inner_focus = INNER_KIND;
        if (combo_.selected_idx != sel_) select(combo_.selected_idx);
        return true;
    }
    if (!pathUsable() || ev.y != starty + pathY()) return false;

    if (ev.x >= startx + FIELD_X && ev.x < startx + FIELD_X + FIELD_W) {
        setGroupFocus(GRP_COMPILER);
        groups()[GRP_COMPILER].inner_focus = INNER_PATH;
        path_.cursor = std::min((int)path_.text.size(), path_.scroll + (ev.x - startx - FIELD_X));
        return true;
    }
    if (ev.x >= startx + BROWSE_X && ev.x < startx + BROWSE_X + BROWSE_W) {
        pressBrowse();
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  NewCompilerDialog
// ─────────────────────────────────────────────────────────────────────────────

std::vector<NewCompilerDialog::Base> NewCompilerDialog::availableBases()
{
    static const std::pair<const char*, const char*> kBases[] = {
        { "gcc",     "GCC-compatible: GCC's options, -o, -std=" },
        { "clang",   "Clang-compatible: GCC's options, Clang's set-up" },
        { "msvc",    "MSVC-compatible: cl.exe's options" },
        { "watcom",  "Open Watcom-compatible: owcc's options" },
        { "generic", "Generic: no options of its own, just -o" },
    };
    std::vector<Base> out;
    for (const auto& [id, label] : kBases)
        if (compilers::Registry::instance().find(id)) out.push_back({ id, label });
    return out;
}

NewCompilerDialog::NewCompilerDialog(Renderer& renderer, const std::string& exe)
    : DialogBase("New Compiler", W, height((int)availableBases().size()))
    , renderer_(renderer)
    , bases_   (availableBases())
{
    exe_.set(exe);
    suggest();
}

std::string NewCompilerDialog::show(Renderer& renderer, std::string& exe)
{
    NewCompilerDialog dlg(renderer, exe);
    DialogResult res = dlg.run(renderer);
    if (!res.accepted()) return "";
    exe = dlg.exe_.text;
    return dlg.created_;
}

// Guess what the executable is like from its name, and for a GCC-style
// driver whether it builds for this computer.
void NewCompilerDialog::suggest()
{
    const std::string exe = exe_.text;
    std::error_code ec;
    if (exe.empty() || !fs::is_regular_file(exe, ec)) return;   // not (yet) a file to look at
    std::string stem = fs::path(exe).stem().string();
    if (!name_edited_) name_.set(stem);
    std::transform(stem.begin(), stem.end(), stem.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    std::string base = "generic";
    if (stem.find("clang") != std::string::npos)                                  base = "clang";
    else if (stem.find("g++") != std::string::npos || stem.find("gcc") != std::string::npos ||
             stem.find("c++") != std::string::npos)                               base = "gcc";
    else if (stem == "cl")                                                        base = "msvc";
    else if (stem == "owcc" || stem.rfind("wcl", 0) == 0 || stem.rfind("wpp", 0) == 0) base = "watcom";
    for (int i = 0; i < (int)bases_.size(); ++i)
        if (bases_[i].id == base) base_ = i;

    cross_ = false;
    if (base == "gcc" || base == "clang") {
        std::string triple = toolchain::runOutput(exe, "-dumpmachine");
        triple.erase(triple.find_last_not_of(" \t\r\n") + 1);
        cross_ = !compilers::targetIsHost(triple);
    }
}

void NewCompilerDialog::onInit()
{
    {
        FocusGroup g;
        g.title = ""; g.hotkey = '\0';
        g.box_w = 0; g.box_h = 0; g.box_x = 0; g.box_y = 0;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    addButtons(ButtonRow{
        .buttons = {
            Button{
                .label = " C&reate ",
                .x = W/2 - 12, .y = btnY(),
                .on_activate = [this]() -> HandleResult {
                    std::error_code ec;
                    if (exe_.text.empty() || !fs::is_regular_file(exe_.text, ec)) {
                        MessageDialog::show(renderer_, "Give the compiler's executable - "
                                                       "Browse can find it.");
                        return HandleResult::CONTINUE;
                    }
                    std::string err;
                    created_ = compilers::Registry::instance().addUser(
                        name_.text, bases_[base_].id, exe_.text, cross_, err);
                    if (created_.empty()) {
                        MessageDialog::show(renderer_, err);
                        return HandleResult::CONTINUE;
                    }
                    result().accept();
                    return HandleResult::CLOSE;
                }
            },
            Button{
                .label = " &Cancel ",
                .x = W/2 + 2, .y = btnY(),
                .on_activate = [this]() -> HandleResult {
                    result().cancel();
                    return HandleResult::CLOSE;
                }
            },
        }
    });

    setGroupFocus(0);
    groups()[0].inner_focus = exe_.text.empty() ? INNER_EXE : INNER_NAME;
}

void NewCompilerDialog::browse()
{
    const std::vector<FilterEntry> filters = {
#ifdef _WIN32
        { "Programs (*.exe)", "*.exe" },
#endif
        { "All files (*)", "*" },
    };
    const std::string picked = pickExecutable(renderer_, "Locate the compiler", filters,
                                              startDirFor(exe_.text));
    if (picked.empty()) return;
    exe_.set(picked);
    suggest();
}

void NewCompilerDialog::pressBrowse()
{
    setGroupFocus(0);
    groups()[0].inner_focus = INNER_BROWSE;
    armCustomPress([this]() { browse(); return HandleResult::CONTINUE; });
}

void NewCompilerDialog::onDraw(Renderer& renderer, int sx, int sy)
{
    const bool focused = (getFocusedGroup() == 0);
    const int  inner   = groups()[0].inner_focus;

    renderer.drawText(sx + 3, sy + NAME_Y, "Name:", Renderer::CP_DIALOG);
    name_.draw(renderer, sx + FIELD_X, sy + NAME_Y, NAME_W);

    renderer.drawText(sx + 3, sy + EXE_Y, "Executable:", Renderer::CP_DIALOG);
    exe_.draw(renderer, sx + FIELD_X, sy + EXE_Y, EXE_W);
    renderer.drawButton(sx + BROWSE_X, sy + EXE_Y, " &Browse ",
                        focused && inner == INNER_BROWSE, customPressed());

    renderer.drawText(sx + 3, sy + BASE_Y, "Based on:", Renderer::CP_DIALOG);
    for (int i = 0; i < (int)bases_.size(); ++i) {
        const bool cur = focused && inner == INNER_BASE && i == base_;
        renderer.drawText(sx + FIELD_X, sy + BASE_Y + i, i == base_ ? "(\xe2\x80\xa2)" : "( )",
                          cur ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
        renderer.drawText(sx + FIELD_X + 4, sy + BASE_Y + i, bases_[i].label, Renderer::CP_DIALOG);
    }

    const bool cross_focused = focused && inner == INNER_CROSS;
    renderer.drawText(sx + FIELD_X, sy + crossY(), cross_ ? "[X]" : "[ ]",
                      cross_focused ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
    renderer.drawText(sx + FIELD_X + 4, sy + crossY(),
                      "Cross compiler (its programs run elsewhere)", Renderer::CP_DIALOG);
}

bool NewCompilerDialog::onPlaceCursor(Renderer& renderer, int sx, int sy)
{
    const int inner = groups()[0].inner_focus;
    if (getFocusedGroup() == 0 && inner == INNER_NAME) {
        renderer.showCursor();
        move(sy + NAME_Y, name_.cursorX(sx + FIELD_X, NAME_W));
        return true;
    }
    if (getFocusedGroup() == 0 && inner == INNER_EXE) {
        renderer.showCursor();
        move(sy + EXE_Y, exe_.cursorX(sx + FIELD_X, EXE_W));
        return true;
    }
    renderer.hideCursor();
    return true;
}

HandleResult NewCompilerDialog::onKey(wint_t ch)
{
    if (getFocusedGroup() != 0) return HandleResult::CONTINUE;
    auto& g = groups()[0];

    if (ch == KEY_UP || ch == KEY_DOWN) {
        if (g.inner_focus == INNER_BASE) {
            if (ch == KEY_DOWN && base_ + 1 < (int)bases_.size()) { ++base_; return HandleResult::CONTINUE; }
            if (ch == KEY_UP && base_ > 0)                        { --base_; return HandleResult::CONTINUE; }
        }
        if (ch == KEY_DOWN && g.inner_focus < INNER_CROSS) ++g.inner_focus;
        if (ch == KEY_UP   && g.inner_focus > INNER_NAME)  --g.inner_focus;
        return HandleResult::CONTINUE;
    }

    switch (g.inner_focus) {
    case INNER_NAME:
        if (name_.key(ch)) name_edited_ = true;
        break;
    case INNER_EXE:
        if (exe_.key(ch)) suggest();
        break;
    case INNER_BROWSE:
        if (ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13) pressBrowse();
        break;
    case INNER_CROSS:
        if (ch == ' ') cross_ = !cross_;
        break;
    default:
        break;
    }
    return HandleResult::CONTINUE;
}

bool NewCompilerDialog::onTab(bool forward)
{
    std::vector<std::pair<int, int>> stops;
    for (int i = INNER_NAME; i <= INNER_CROSS; ++i) stops.push_back({ 0, i });
    for (int b = 0; b < BTN_COUNT; ++b) stops.push_back({ groupCount(), b });

    const std::pair<int, int> cur = inButtonRow()
        ? std::make_pair(groupCount(), getBtnInnerFocus())
        : std::make_pair(0, groups()[0].inner_focus);
    int idx = 0;
    for (int i = 0; i < (int)stops.size(); ++i)
        if (stops[i] == cur) { idx = i; break; }
    const int n = (int)stops.size();
    idx = forward ? (idx + 1) % n : (idx + n - 1) % n;

    setGroupFocus(stops[idx].first);
    if (stops[idx].first == groupCount()) setGroupBtnFocus(stops[idx].second);
    else groups()[0].inner_focus = stops[idx].second;
    return true;
}

bool NewCompilerDialog::onAltKey(char lower)
{
    if (lower == 'b') { pressBrowse(); return true; }
    return false;
}

bool NewCompilerDialog::onMouseClick(const MEVENT& ev, int startx, int starty)
{
    auto& g = groups()[0];
    auto in = [&](int y, int x0, int w) {
        return ev.y == starty + y && ev.x >= startx + x0 && ev.x < startx + x0 + w;
    };
    if (in(NAME_Y, FIELD_X, NAME_W)) {
        setGroupFocus(0); g.inner_focus = INNER_NAME;
        name_.cursor = std::min((int)name_.text.size(), name_.scroll + (ev.x - startx - FIELD_X));
        return true;
    }
    if (in(EXE_Y, FIELD_X, EXE_W)) {
        setGroupFocus(0); g.inner_focus = INNER_EXE;
        exe_.cursor = std::min((int)exe_.text.size(), exe_.scroll + (ev.x - startx - FIELD_X));
        return true;
    }
    if (in(EXE_Y, BROWSE_X, BROWSE_W)) { pressBrowse(); return true; }
    for (int i = 0; i < (int)bases_.size(); ++i)
        if (in(BASE_Y + i, FIELD_X, W - FIELD_X - 3)) {
            setGroupFocus(0); g.inner_focus = INNER_BASE; base_ = i;
            return true;
        }
    if (in(crossY(), FIELD_X, W - FIELD_X - 3)) {
        setGroupFocus(0); g.inner_focus = INNER_CROSS; cross_ = !cross_;
        return true;
    }
    return false;
}
