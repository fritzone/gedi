#include "CompileOptionsDialog.h"
#include "BuildSystem.h"
#include "utils.h"
#include <ncurses.h>
#include <cstdio>

// ── helpers ───────────────────────────────────────────────────────────────────

static std::vector<OptionList::Option> buildOptions(const CompilerSettings& s, int tab)
{
    using O = OptionList::Option;
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

// ── Constructor ───────────────────────────────────────────────────────────────

CompileOptionsDialog::CompileOptionsDialog(Renderer&          renderer,
                                           BuildSystem&       buildSystem,
                                           CompilerSettings&  settings,
                                           const GediProject* project,
                                           const std::string& filename)
    : DialogBase(project
                     ? ("Build Options \xe2\x80\x94 " + project->name)
                     : "Compiler Options",
                 W, H)
    , renderer_   (renderer)
    , buildSystem_(buildSystem)
    , settings_   (settings)
    , project_    (project)
    , filename_   (filename)
    , temp_       (settings)
    , standards_  ({"c++11","c++14","c++17","c++20","c++23","c++26"})
{
    for (int i = 0; i < (int)standards_.size(); ++i)
        if (standards_[i] == temp_.cpp_standard) { std_idx_ = i; break; }
}

// ── Static factory ────────────────────────────────────────────────────────────

void CompileOptionsDialog::show(Renderer&          renderer,
                                BuildSystem&       buildSystem,
                                CompilerSettings&  settings,
                                const GediProject* project,
                                const std::string& filename)
{
    CompileOptionsDialog dlg(renderer, buildSystem, settings, project, filename);
    dlg.run(renderer);
}

// ── onInit ────────────────────────────────────────────────────────────────────

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
        g.tabcontrols.push_back(TabControl({"Basic","Advanced","Expert"}, 2, TAB_Y, W - 4));
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

    // Button row
    addButtons(ButtonRow{
        .buttons = {
            Button{
                .label = " &Ok ",
                .x = W/2 - 20, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    settings_ = temp_;
                    result().accept();
                    return HandleResult::CLOSE;
                }
            },
            Button{
                .label = " Cop&y ",
                .x = W/2 - 6, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    std::string cmd;
                    if (project_) {
                        cmd = BuildSystem::buildProjectPreview(*project_, temp_);
                    } else {
                        std::string base = buildSystem_.guessCompileCommand(filename_);
                        cmd = buildSystem_.get_full_compile_command(base, temp_);
                    }
                    FILE* p = popen("xclip -selection clipboard -i", "w");
                    if (p) { fputs(cmd.c_str(), p); pclose(p); }
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

// ── onDraw ────────────────────────────────────────────────────────────────────

void CompileOptionsDialog::onDraw(Renderer& renderer, int startx, int starty)
{
    renderer.drawText(startx + 2, starty + COMBO_Y, "C++ Standard:", Renderer::CP_DIALOG);

    // Sync ComboBox → temp_.cpp_standard every frame
    if (!groups()[GRP_STANDARD].comboboxes.empty())
        temp_.cpp_standard = groups()[GRP_STANDARD].comboboxes[0].selectedText();

    // Sync OptionList to active tab
    if (!groups()[GRP_TABS].tabcontrols.empty() &&
        !groups()[GRP_OPTIONS].optionlists.empty())
    {
        int active = groups()[GRP_TABS].tabcontrols[0].activeTab();
        groups()[GRP_OPTIONS].optionlists[0].options = buildOptions(temp_, active);
    }

    // Optional flags field content
    renderer.drawText(startx + 4, starty + FLAGS_BOX_Y + 1,
                      std::string(W - 8, ' '), Renderer::CP_LIST_BOX);
    renderer.drawText(startx + 4, starty + FLAGS_BOX_Y + 1,
                      temp_.optional_flags, Renderer::CP_LIST_BOX);

    // Command preview (project mode or per-file mode)
    std::string cmd;
    if (project_) {
        cmd = BuildSystem::buildProjectPreview(*project_, temp_);
    } else {
        std::string base = buildSystem_.guessCompileCommand(filename_);
        cmd = buildSystem_.get_full_compile_command(base, temp_);
    }
    auto wrapped = wrap_text(cmd, W - 8);

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

// ── onKey ─────────────────────────────────────────────────────────────────────

HandleResult CompileOptionsDialog::onKey(wint_t ch)
{
    if (getFocusedGroup() == GRP_CMD) {
        if (ch == KEY_UP   && cmd_top_row_ > 0) { --cmd_top_row_; return HandleResult::CONTINUE; }
        if (ch == KEY_DOWN)                      { ++cmd_top_row_; return HandleResult::CONTINUE; }
    }
    return HandleResult::CONTINUE;
}
