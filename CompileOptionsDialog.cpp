#include "CompileOptionsDialog.h"
#include "BuildSystem.h"
#include "DependencyChecker.h"
#include "FileBrowser.h"
#include "MessageDialog.h"
#include "utils.h"
#include "curses_compat.h"
#include "platform_compat.h"
#include <algorithm>
#include <cstdio>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

static std::vector<OptionList::Option> buildOptions(const CompilerSettings& s, int tab)
{
    CompilerSettings& t = const_cast<CompilerSettings&>(s);

    if (tab == 0) return {
        { "Generate debug symbols (-g)",          "Optimization", false, 0, &t.debug_symbols,        nullptr },
        { "Disable optimisation (-O0)",            "Optimization", true,  0, nullptr,                 &t.optimization_level },
        { "Balanced optimisation (-O2)",           "Optimization", true,  1, nullptr,                 &t.optimization_level },
        { "High optimisation (-O3)",               "Optimization", true,  2, nullptr,                 &t.optimization_level },
        { "Enable common warnings (-Wall)",        "Warnings",     false, 0, &t.wall,                 nullptr },
        { "Enable extra warnings (-Wextra)",       "Warnings",     false, 0, &t.wextra,               nullptr },
        { "Strict ISO compliance (-Wpedantic)",    "Warnings",     false, 0, &t.wpedantic,            nullptr },
        { "Treat warnings as errors (-Werror)",    "Warnings",     false, 0, &t.werror,               nullptr },
    };

    if (tab == 1) return {
        { "Warn on implicit conversions (-Wconversion)",           "Warnings",  false, 0, &t.wconversion,          nullptr },
        { "Warn on signed/unsigned conv (-Wsign-conversion)",      "Warnings",  false, 0, &t.wsign_conversion,     nullptr },
        { "Warn on variable shadowing (-Wshadow)",                 "Warnings",  false, 0, &t.wshadow,              nullptr },
        { "Warn on non-virtual destructors (-Wnon-virtual-dtor)",  "Warnings",  false, 0, &t.wnon_virtual_dtor,    nullptr },
        { "Warn on old-style casts (-Wold-style-cast)",            "Warnings",  false, 0, &t.wold_style_cast,      nullptr },
        { "Warn on overloaded virtuals (-Woverloaded-virtual)",    "Warnings",  false, 0, &t.woverloaded_virtual,  nullptr },
        { "Warn on null dereference (-Wnull-dereference)",         "Warnings",  false, 0, &t.wnull_dereference,    nullptr },
        { "Warn on double promotion (-Wdouble-promotion)",         "Warnings",  false, 0, &t.wdouble_promotion,    nullptr },
        { "Strict format string checking (-Wformat=2)",            "Warnings",  false, 0, &t.wformat_2,            nullptr },
        { "Keep frame pointer (-fno-omit-frame-pointer)",          "Sanitizer", false, 0, &t.fno_omit_frame_pointer, nullptr },
        { "Enable ASan and UBSan (-fsanitize=address,ub)",         "Sanitizer", false, 0, &t.fsanitize_address_ub, nullptr },
        { "Enable LeakSanitizer (-fsanitize=leak)",                "Sanitizer", false, 0, &t.fsanitize_leak,       nullptr },
        { "Enable LTO (-flto)",                                    "Sanitizer", false, 0, &t.flto,                 nullptr },
        { "Optimise for host CPU (-march=native)",                 "Optimizer", false, 0, &t.march_native,         nullptr },
        { "Tune for host CPU (-mtune=native)",                     "Optimizer", false, 0, &t.mtune_native,         nullptr },
    };

    return {
        { "Warn on cast alignment (-Wcast-align)",                 "Warnings",      false, 0, &t.wcast_align,               nullptr },
        { "Warn on cast qualifiers (-Wcast-qual)",                 "Warnings",      false, 0, &t.wcast_qual,                nullptr },
        { "Warn on missing enum cases (-Wswitch-enum)",            "Warnings",      false, 0, &t.wswitch_enum,              nullptr },
        { "Warn on undefined macros (-Wundef)",                    "Warnings",      false, 0, &t.wundef,                    nullptr },
        { "Warn on redundant decls (-Wredundant-decls)",           "Warnings",      false, 0, &t.wredundant_decls,          nullptr },
        { "Warn on logical op issues (-Wlogical-op)",              "Warnings",      false, 0, &t.wlogical_op,               nullptr },
        { "Warn on useless casts (-Wuseless-cast)",                "Warnings",      false, 0, &t.wuseless_cast,             nullptr },
        { "Effective C++ warnings (-Weffc++)",                     "Warnings",      false, 0, &t.weffcxx,                   nullptr },
        { "Disable exceptions (-fno-exceptions)",                  "Functionality", false, 0, &t.fno_exceptions,            nullptr },
        { "Disable RTTI (-fno-rtti)",                              "Functionality", false, 0, &t.fno_rtti,                  nullptr },
        { "Hide symbols by default (-fvisibility=hidden)",         "Functionality", false, 0, &t.fvisibility_hidden,        nullptr },
        { "Enable strict aliasing (-fstrict-aliasing)",            "Functionality", false, 0, &t.fstrict_aliasing,          nullptr },
        { "Sanitise ptr comparisons (-fsanitize=ptr-cmp)",         "Sanitizer",     false, 0, &t.fsanitize_pointer_compare, nullptr },
        { "Sanitise ptr arithmetic (-fsanitize=ptr-sub)",          "Sanitizer",     false, 0, &t.fsanitize_pointer_subtract,nullptr },
        { "Linker: remove unused deps (--as-needed)",              "Linker",        false, 0, &t.wl_as_needed,              nullptr },
        { "Linker: optimise linking (-Wl,-O1)",                    "Linker",        false, 0, &t.wl_o1,                     nullptr },
    };
}

// Which kind the recorded toolchain is. Normally it says so outright, but one
// that gedi probed for itself has no "kind" at all - so fall back to reading the
// compiler's own name, which is how the probe found it in the first place.
static toolchain::Kind kindOf(const Toolchain& tc)
{
    const toolchain::Kind recorded = toolchain::kindFromId(tc.kind);
    if (recorded != toolchain::Kind::None) return recorded;

    if (BuildSystem::isMsvcCompiler(tc.cxx)) return toolchain::Kind::Msvc;

    std::string stem = std::filesystem::path(tc.cxx).stem().string();
    std::transform(stem.begin(), stem.end(), stem.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (stem.find("clang") != std::string::npos) return toolchain::Kind::Clang;
    if (stem.find("wpp")   != std::string::npos ||
        stem.find("owcc")  != std::string::npos) return toolchain::Kind::Watcom;
    if (stem.find("g++")   != std::string::npos ||
        stem.find("gcc")   != std::string::npos) return toolchain::Kind::Gcc;
    return toolchain::Kind::None;
}

CompileOptionsDialog::CompileOptionsDialog(Renderer&          renderer,
                                           BuildSystem&       buildSystem,
                                           Config&            config,
                                           CompilerSettings&  settings,
                                           const GediProject* project,
                                           const std::string& filename)
    : DialogBase(project
                     ? ("Build Options \xe2\x80\x94 " + project->name)
                     : "Compiler Options",
                 W, H)
    , renderer_   (renderer)
    , buildSystem_(buildSystem)
    , config_     (config)
    , settings_   (settings)
    , project_    (project)
    , filename_   (filename)
    , temp_       (settings)
    , standards_  ({"c++11","c++14","c++17","c++20","c++23","c++26"})
{
    for (int i = 0; i < (int)standards_.size(); ++i)
        if (standards_[i] == temp_.cpp_standard) { std_idx_ = i; break; }

    // The Compiler tab offers the four real toolchains; "none" is only a thing
    // for setup, which has to let the user defer the choice.
    for (toolchain::Kind k : toolchain::kinds())
        if (k != toolchain::Kind::None) tc_kinds_.push_back(k);

    // Start on whatever is in use, so the tab opens showing the status quo
    // rather than proposing a change nobody asked for.
    const toolchain::Kind cur = kindOf(config_.toolchain);
    for (int i = 0; i < (int)tc_kinds_.size(); ++i)
        if (tc_kinds_[i] == cur) { tc_sel_ = i; break; }
    if (toolchain::needsPath(cur)) {
        tc_path_        = config_.toolchain.cxx;
        tc_path_cursor_ = (int)tc_path_.size();
    }
}

void CompileOptionsDialog::show(Renderer&          renderer,
                                BuildSystem&       buildSystem,
                                Config&            config,
                                CompilerSettings&  settings,
                                const GediProject* project,
                                const std::string& filename)
{
    CompileOptionsDialog dlg(renderer, buildSystem, config, settings, project, filename);
    dlg.run(renderer);
}

bool CompileOptionsDialog::compilerTab()
{
    const auto& tabs = groups()[GRP_TABS].tabcontrols;
    return !tabs.empty() && tabs[0].activeTab() == TAB_COMPILER;
}

bool CompileOptionsDialog::tcPathUsable() const
{
    return toolchain::needsPath(tc_kinds_[tc_sel_]);
}

void CompileOptionsDialog::probeCompiler()
{
    const toolchain::Kind k = tc_kinds_[tc_sel_];
    pending_ = {};
    tc_error_.clear();

    // Visual Studio locates itself; the rest are built from the path given, and
    // an empty one is "not answered yet" rather than something to complain about.
    if (k == toolchain::Kind::Msvc) {
        pending_ = toolchain::detectMsvc(tc_error_);
    } else if (!tc_path_.empty()) {
        pending_ = toolchain::fromPath(k, tc_path_, tc_error_);
    }
}

void CompileOptionsDialog::onInit()
{
    // Group 0: C++ Standard
    {
        FocusGroup g;
        g.title = ""; g.hotkey = '\0';
        g.box_w = 0; g.box_h = 0; g.box_x = 0; g.box_y = 0;
        g.comboboxes.push_back(ComboBox(standards_, std_idx_, 17, COMBO_Y, 12));
        addGroup(std::move(g));
    }

    // Group 1: Tab bar
    {
        FocusGroup g;
        g.title = ""; g.hotkey = '\0';
        g.box_w = 0; g.box_h = 0; g.box_x = 0; g.box_y = 0;
        g.tabcontrols.push_back(
            TabControl({"Basic","Advanced","Expert","Compiler"}, 2, TAB_Y, W - 4));
        addGroup(std::move(g));
    }

    // Group 2: Option list
    {
        FocusGroup g;
        g.title = ""; g.hotkey = '\0';
        g.box_w = 0; g.box_h = 0; g.box_x = 0; g.box_y = 0;
        OptionList ol;
        ol.x = 2; ol.y = OPT_Y;
        ol.visible_rows = OPT_ROWS;
        ol.options = buildOptions(temp_, 0);
        g.optionlists.push_back(std::move(ol));
        addGroup(std::move(g));
    }

    // Group 3: Optional flags
    {
        FocusGroup g;
        g.title = " Optional flags "; g.hotkey = '\0';
        g.box_x = 2; g.box_y = FLAGS_BOX_Y;
        g.box_w = W - 4; g.box_h = FLAGS_BOX_H;
        g.text_buffer = &temp_.optional_flags;
        addGroup(std::move(g));
    }

    // Group 4: Command preview (title adapts to mode)
    {
        FocusGroup g;
        g.title  = project_ ? " Build Command " : " Final Command ";
        g.hotkey = '\0';
        g.box_x  = 2; g.box_y = CMD_BOX_Y;
        g.box_w  = W - 4; g.box_h = CMD_BOX_H;
        addGroup(std::move(g));
    }

    // Group 5: Compiler tab. Everything on it is painted by onDraw, so the
    // framework only needs to know it exists and can take focus.
    {
        FocusGroup g;
        g.title = ""; g.hotkey = '\0';
        g.box_w = 0; g.box_h = 0; g.box_x = 0; g.box_y = 0;
        g.draw_widgets_manually = true;
        addGroup(std::move(g));
    }

    current_desc_ = toolchain::versionOf(config_.toolchain.cxx);

    // Button row
    addButtons(ButtonRow{
        .buttons = {
            Button{
                .label = " &Ok ",
                .x = W/2 - 20, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    // The compiler is a global setting and can fail to apply, so
                    // it goes first: a failure there must not look like the
                    // per-file options failed too.
                    if (tc_changed_) {
                        if (tc_dirty_) { probeCompiler(); tc_dirty_ = false; }
                        if (!pending_.ok()) {
                            MessageDialog::show(renderer_,
                                tc_error_.empty()
                                    ? std::string("No compiler was selected.")
                                    : tc_error_);
                            return HandleResult::CONTINUE;
                        }
                        applyCompiler();
                    }
                    settings_ = temp_;
                    result().accept();
                    return HandleResult::CLOSE;
                }
            },
            Button{
                .label = " Cop&y ",
                .x = W/2 - 6, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    const std::string cmd = previewCommand();
#ifdef _WIN32
                    if (OpenClipboard(nullptr)) {
                        EmptyClipboard();
                        HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, cmd.size() + 1);
                        if (hmem) {
                            memcpy(GlobalLock(hmem), cmd.c_str(), cmd.size() + 1);
                            GlobalUnlock(hmem);
                            SetClipboardData(CF_TEXT, hmem);
                        }
                        CloseClipboard();
                    }
#else
                    FILE* p = popen("xclip -selection clipboard -i", "w");
                    if (p) { fputs(cmd.c_str(), p); pclose(p); }
#endif
                    return HandleResult::CONTINUE;
                }
            },
            Button{
                .label = " &Cancel ",
                .x = W/2 + 8, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    result().cancel();
                    return HandleResult::CLOSE;
                }
            },
        }
    });

    setGroupFocus(GRP_STANDARD);
    setGroupBtnFocus(0);
}

// The command the preview box shows and Copy puts on the clipboard. A compiler
// the user has picked but not yet accepted is used for it, so the preview never
// quietly names the one that is being replaced.
std::string CompileOptionsDialog::previewCommand()
{
    const std::string cxx = (tc_changed_ && pending_.ok()) ? pending_.cxx : std::string();
    if (project_) return buildSystem_.buildProjectPreview(*project_, temp_, cxx);
    return buildSystem_.previewWithCompiler(filename_, temp_, cxx);
}

void CompileOptionsDialog::applyCompiler()
{
    std::string err;
    // Keep the python already recorded - write() merges, so nothing else in the
    // file (pkg-config, the --ignore-dependencies opt-out) is disturbed either.
    if (!toolchain::write(pending_, config_.toolchain.python3, err)) {
        MessageDialog::show(renderer_, "The compiler could not be saved:\n" + err);
        return;
    }

    const bool is_clang = (pending_.kind == toolchain::Kind::Clang);
    Toolchain& tc = config_.toolchain;
    tc.kind       = toolchain::id(pending_.kind);
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
    tc_changed_   = false;
}

void CompileOptionsDialog::browseForCompiler()
{
    if (!tcPathUsable()) return;        // MSVC finds itself; nothing to browse for

    const toolchain::Kind k = tc_kinds_[tc_sel_];
    const std::vector<FilterEntry> filters = {
        { std::string(toolchain::exeName(k)) + " and similar", toolchain::browsePattern(k) },
        { "Programs (*.exe)", "*.exe" },
        { "All files (*)",    "*" },
    };
    const std::string start = toolchain::suggestedDir(k, tc_path_);
    const std::string title = std::string("Locate ") + toolchain::label(k);

    // FileBrowser navigates by chdir and leaves us wherever the user stopped.
    // The editor resolves relative paths against its own working directory, so
    // it has to come back.
    char saved[1024];
    const bool have_saved = getcwd(saved, sizeof saved) != nullptr;

    const std::string picked = FileBrowser::open(renderer_, title, filters, start);

    if (have_saved) { const int rc = chdir(saved); (void)rc; }
    if (picked.empty()) return;         // cancelled - leave the field alone

    tc_path_        = picked;
    tc_path_cursor_ = (int)tc_path_.size();
    tc_path_scroll_ = 0;
    tc_dirty_       = true;
    tc_changed_     = true;
}

void CompileOptionsDialog::pressBrowse()
{
    groups()[GRP_COMPILER].inner_focus = TC_INNER_BROWSE;
    armCustomPress([this]() { browseForCompiler(); return HandleResult::CONTINUE; });
}

//  The Compiler tab's content, painted where the option list otherwise sits.
void CompileOptionsDialog::drawCompilerTab(Renderer& renderer, int sx, int sy)
{
    if (tc_dirty_) { probeCompiler(); tc_dirty_ = false; }

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
    const toolchain::Kind cur_kind = kindOf(config_.toolchain);
    std::string cur = "In use: ";
    cur += (cur_kind == toolchain::Kind::None) ? "(not recorded)" : toolchain::label(cur_kind);
    if (!current_desc_.empty()) cur += "  \xe2\x80\x94  " + current_desc_;
    line(TC_CUR_Y, cur, Renderer::CP_DIALOG, A_BOLD);
    line(TC_CURP_Y, config_.toolchain.cxx.empty()
                        ? std::string("        (no compiler recorded)")
                        : "        " + config_.toolchain.cxx,
         Renderer::CP_DIALOG);

    //  Pick one
    renderer.drawText(sx + 3, sy + TC_KIND_Y - 1, "Use:", Renderer::CP_DIALOG);
    for (int i = 0; i < (int)tc_kinds_.size(); ++i) {
        const bool item_focused = grp_focused && inner == TC_INNER_KIND && i == tc_sel_;
        renderer.drawText(sx + 8, sy + TC_KIND_Y + i,
                          i == tc_sel_ ? "(\xe2\x80\xa2)" : "( )",
                          item_focused ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
        std::string label = toolchain::label(tc_kinds_[i]);
        label.resize(36, ' ');
        renderer.drawText(sx + 12, sy + TC_KIND_Y + i, label, Renderer::CP_DIALOG);
    }

    //  Where it is
    if (tcPathUsable()) {
        renderer.drawText(sx + 3, sy + TC_PATH_Y, "Path:", Renderer::CP_DIALOG);
        if (tc_path_cursor_ - tc_path_scroll_ >= TC_FIELD_W)
            tc_path_scroll_ = tc_path_cursor_ - TC_FIELD_W + 1;
        if (tc_path_cursor_ < tc_path_scroll_) tc_path_scroll_ = tc_path_cursor_;

        renderer.drawText(sx + TC_FIELD_X, sy + TC_PATH_Y,
                          std::string(TC_FIELD_W, ' '), Renderer::CP_LIST_BOX);
        if ((int)tc_path_.size() > tc_path_scroll_)
            renderer.drawText(sx + TC_FIELD_X, sy + TC_PATH_Y,
                              tc_path_.substr(tc_path_scroll_, TC_FIELD_W),
                              Renderer::CP_LIST_BOX);

        renderer.drawButton(sx + TC_BROWSE_X, sy + TC_PATH_Y, " &Browse ",
                            grp_focused && inner == TC_INNER_BROWSE, customPressed());
    } else {
        line(TC_PATH_Y, "Located automatically.", Renderer::CP_DIALOG);
    }

    //  What the pick resolved to
    std::string note;
    int color = Renderer::CP_DIALOG;
    if (pending_.ok()) {
        for (const auto& n : pending_.notes) note += (note.empty() ? "" : "   ") + n;
    } else if (!tc_error_.empty()) {
        note  = tc_error_;
        color = Renderer::CP_COMPILE_ERROR;
    }
    // Stop short of the Browse button's drop shadow, which falls on this row.
    std::string shown = note;
    const int note_w = TC_BROWSE_X - 4;
    if ((int)shown.size() > note_w) shown = shown.substr(0, note_w - 3) + "...";
    shown.resize(note_w, ' ');
    renderer.drawText(sx + 3, sy + TC_NOTE_Y, shown, color);
}

void CompileOptionsDialog::onDraw(Renderer& renderer, int startx, int starty)
{
    renderer.drawText(startx + 2, starty + COMBO_Y, "C++ Standard:", Renderer::CP_DIALOG);

    // Sync ComboBox → temp_.cpp_standard every frame
    if (!groups()[GRP_STANDARD].comboboxes.empty())
        temp_.cpp_standard = groups()[GRP_STANDARD].comboboxes[0].selectedText();

    // The Compiler tab replaces the option list in the same rows. Suppressing
    // the list is a matter of telling its group not to paint itself and zeroing
    // its box, so it also stops catching clicks meant for the compiler controls.
    const bool on_compiler = compilerTab();
    if (!groups()[GRP_OPTIONS].optionlists.empty()) {
        groups()[GRP_OPTIONS].draw_widgets_manually = on_compiler;
        if (!on_compiler) {
            int active = groups()[GRP_TABS].tabcontrols[0].activeTab();
            groups()[GRP_OPTIONS].optionlists[0].options = buildOptions(temp_, active);
        }
    }

    // Focus must never be left on a tab that is no longer showing.
    if (on_compiler) {
        if (getFocusedGroup() == GRP_OPTIONS) setGroupFocus(GRP_COMPILER);
        drawCompilerTab(renderer, startx, starty);
    } else if (getFocusedGroup() == GRP_COMPILER) {
        setGroupFocus(GRP_OPTIONS);
    }

    // Optional flags field content
    renderer.drawText(startx + 4, starty + FLAGS_BOX_Y + 1,
                      std::string(W - 8, ' '), Renderer::CP_LIST_BOX);
    renderer.drawText(startx + 4, starty + FLAGS_BOX_Y + 1,
                      temp_.optional_flags, Renderer::CP_LIST_BOX);

    auto wrapped = wrap_text(previewCommand(), W - 8);

    int max_top = std::max(0, (int)wrapped.size() - CMD_VIEW_H);
    if (cmd_top_row_ < 0)       cmd_top_row_ = 0;
    if (cmd_top_row_ > max_top) cmd_top_row_ = max_top;

    bool cmd_focused = (getFocusedGroup() == GRP_CMD);
    for (int i = 0; i < CMD_VIEW_H; ++i) {
        int line = cmd_top_row_ + i;
        std::string text = (line < (int)wrapped.size()) ? wrapped[line] : "";
        if ((int)text.size() < W - 8)
            text += std::string(W - 8 - text.size(), ' ');
        renderer.drawText(startx + 4, starty + CMD_BOX_Y + 1 + i,
                          text,
                          cmd_focused ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG);
    }
}

HandleResult CompileOptionsDialog::onKey(wint_t ch)
{
    if (getFocusedGroup() == GRP_CMD) {
        if (ch == KEY_UP   && cmd_top_row_ > 0) { --cmd_top_row_; return HandleResult::CONTINUE; }
        if (ch == KEY_DOWN)                      { ++cmd_top_row_; return HandleResult::CONTINUE; }
        return HandleResult::CONTINUE;
    }

    if (getFocusedGroup() != GRP_COMPILER) return HandleResult::CONTINUE;

    auto& g = groups()[GRP_COMPILER];

    // Up/Down walk the controls: through the four kinds first, then on to the
    // path field and Browse, so the whole tab is reachable without Tab.
    if (ch == KEY_UP || ch == KEY_DOWN) {
        const int last = tcPathUsable() ? TC_INNER_BROWSE : TC_INNER_KIND;
        if (ch == KEY_DOWN) {
            if (g.inner_focus == TC_INNER_KIND && tc_sel_ + 1 < (int)tc_kinds_.size()) {
                tc_sel_++; tc_dirty_ = true; tc_changed_ = true;
            } else if (g.inner_focus < last) {
                ++g.inner_focus;
            }
        } else {
            if (g.inner_focus > TC_INNER_KIND) --g.inner_focus;
            else if (tc_sel_ > 0) { tc_sel_--; tc_dirty_ = true; tc_changed_ = true; }
        }
        return HandleResult::CONTINUE;
    }

    if (g.inner_focus == TC_INNER_BROWSE) {
        if (ch == ' ' || ch == KEY_ENTER || ch == 10 || ch == 13) pressBrowse();
        return HandleResult::CONTINUE;
    }

    if (g.inner_focus == TC_INNER_PATH && tcPathUsable()) {
        if (ch == KEY_LEFT)        { if (tc_path_cursor_ > 0) --tc_path_cursor_; }
        else if (ch == KEY_RIGHT)  { if (tc_path_cursor_ < (int)tc_path_.size()) ++tc_path_cursor_; }
        else if (ch == KEY_HOME)   { tc_path_cursor_ = 0; }
        else if (ch == KEY_END)    { tc_path_cursor_ = (int)tc_path_.size(); }
        else if (ch == KEY_BACKSPACE || ch == 8 || ch == 127) {
            if (tc_path_cursor_ > 0) {
                tc_path_.erase(tc_path_cursor_ - 1, 1);
                --tc_path_cursor_;
                tc_dirty_ = tc_changed_ = true;
            }
        } else if (ch == KEY_DC) {
            if (tc_path_cursor_ < (int)tc_path_.size()) {
                tc_path_.erase(tc_path_cursor_, 1);
                tc_dirty_ = tc_changed_ = true;
            }
        } else if (ch > 31 && ch < KEY_MIN) {
            tc_path_.insert(tc_path_.begin() + tc_path_cursor_, (char)ch);
            ++tc_path_cursor_;
            tc_dirty_ = tc_changed_ = true;
        }
        return HandleResult::CONTINUE;
    }

    return HandleResult::CONTINUE;
}

//  Focus order. The option list and the Compiler tab share the same rows, so
//  only one of them is ever a tab stop - whichever the tab bar is showing.
std::vector<int> CompileOptionsDialog::tabStops()
{
    return { GRP_STANDARD, GRP_TABS,
             compilerTab() ? GRP_COMPILER : GRP_OPTIONS,
             GRP_FLAGS, GRP_CMD, groupCount() /* the button row */ };
}

bool CompileOptionsDialog::onTab(bool forward)
{
    const std::vector<int> stops = tabStops();
    const int cur = inButtonRow() ? groupCount() : getFocusedGroup();

    // Inside the button row, Tab steps between the buttons before leaving it.
    if (cur == groupCount()) {
        const int b = getBtnInnerFocus();
        if (forward  && b < BTN_COUNT - 1) { setGroupBtnFocus(b + 1); return true; }
        if (!forward && b > 0)             { setGroupBtnFocus(b - 1); return true; }
    }

    int idx = 0;
    for (int i = 0; i < (int)stops.size(); ++i)
        if (stops[i] == cur) { idx = i; break; }

    idx = forward ? (idx + 1) % (int)stops.size()
                  : (idx + (int)stops.size() - 1) % (int)stops.size();

    const int next = stops[idx];
    setGroupFocus(next);
    if (next == groupCount()) setGroupBtnFocus(forward ? 0 : BTN_COUNT - 1);
    else if (next == GRP_COMPILER) groups()[GRP_COMPILER].inner_focus = TC_INNER_KIND;
    return true;
}

bool CompileOptionsDialog::onMouseClick(const MEVENT& ev, int startx, int starty)
{
    if (!compilerTab()) return false;

    //  A kind row
    for (int i = 0; i < (int)tc_kinds_.size(); ++i) {
        if (ev.y != starty + TC_KIND_Y + i) continue;
        if (ev.x < startx + 8 || ev.x >= startx + W - 3) continue;
        setGroupFocus(GRP_COMPILER);
        groups()[GRP_COMPILER].inner_focus = TC_INNER_KIND;
        if (tc_sel_ != i) { tc_sel_ = i; tc_dirty_ = tc_changed_ = true; }
        return true;
    }

    if (!tcPathUsable()) return false;

    //  The path field
    if (ev.y == starty + TC_PATH_Y &&
        ev.x >= startx + TC_FIELD_X && ev.x < startx + TC_FIELD_X + TC_FIELD_W) {
        setGroupFocus(GRP_COMPILER);
        groups()[GRP_COMPILER].inner_focus = TC_INNER_PATH;
        tc_path_cursor_ = std::min((int)tc_path_.size(),
                                   tc_path_scroll_ + (ev.x - startx - TC_FIELD_X));
        return true;
    }

    //  The Browse button
    if (ev.y == starty + TC_PATH_Y &&
        ev.x >= startx + TC_BROWSE_X && ev.x < startx + TC_BROWSE_X + TC_BROWSE_W) {
        setGroupFocus(GRP_COMPILER);
        pressBrowse();
        return true;
    }

    return false;
}
