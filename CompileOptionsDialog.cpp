#include "CompileOptionsDialog.h"
#include "BuildSystem.h"
#include "MessageDialog.h"
#include "utils.h"
#include "curses_compat.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

static const std::vector<std::string> kDefaultStandards =
    { "c++11", "c++14", "c++17", "c++20", "c++23", "c++26" };

// An option's label as the list shows it: the definition's wording, then the
// switch it adds, so the user sees exactly what goes on the command line.
static std::string withFlag(const std::string& label, const std::string& flag)
{
    return flag.empty() ? label : label + " (" + flag + ")";
}

CompileOptionsDialog::CompileOptionsDialog(Renderer&          renderer,
                                           BuildSystem&       buildSystem,
                                           CompilerSettings&  settings,
                                           const GediProject* project,
                                           const std::string& filename)
    : DialogBase(project
                     ? ("Build Options \xe2\x80\x94 " + project->name)
                     : ("Compile Options \xe2\x80\x94 " + buildSystem.definition().name),
                 W, H)
    , renderer_   (renderer)
    , buildSystem_(buildSystem)
    , def_        (buildSystem.definition())
    , settings_   (settings)
    , project_    (project)
    , filename_   (filename)
    , temp_       (settings)
{
    // Every option bound below needs a value to point at.
    compilers::fillDefaults(temp_, def_);
    vals_ = &temp_.compilers[def_.settings_id];
    unsupported_ = compilers::testedUnsupported(def_, buildSystem.compilerPath());
}

void CompileOptionsDialog::show(Renderer&          renderer,
                                BuildSystem&       buildSystem,
                                CompilerSettings&  settings,
                                const GediProject* project,
                                const std::string& filename)
{
    CompileOptionsDialog dlg(renderer, buildSystem, settings, project, filename);
    dlg.run(renderer);
}

void CompileOptionsDialog::onInit()
{
    // Group 0: the top row - the C++ standard, or the option the compiler's
    // definition puts there instead (a compiler without a standard switch).
    {
        FocusGroup g;
        g.title = ""; g.hotkey = '\0';
        g.box_w = 0; g.box_h = 0; g.box_x = 0; g.box_y = 0;
        int sel = 0;
        if (def_.top) {
            for (const auto& c : def_.top->choices) top_items_.push_back(c.label);
            sel = vals_->values[def_.top->id];
        } else {
            top_items_ = def_.standards.empty() ? kDefaultStandards : def_.standards;
            for (int i = 0; i < (int)top_items_.size(); ++i)
                if (top_items_[i] == temp_.cpp_standard) { sel = i; break; }
        }
        int width = 12;
        for (const auto& it : top_items_) width = std::max(width, (int)it.size() + 6);
        g.comboboxes.push_back(ComboBox(top_items_, sel, 17, COMBO_Y, width));
        addGroup(std::move(g));
    }

    // Group 1: Tab bar
    {
        FocusGroup g;
        g.title = ""; g.hotkey = '\0';
        g.box_w = 0; g.box_h = 0; g.box_x = 0; g.box_y = 0;
        std::vector<std::string> names;
        for (const auto& t : def_.tabs) names.push_back(t.name);
        if (names.empty()) names.push_back("Options");
        g.tabcontrols.push_back(TabControl(names, 2, TAB_Y, W - 4));
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
        ol.width = W - 6;
        ol.options = options(0);
        g.optionlists.push_back(std::move(ol));
        addGroup(std::move(g));
    }

    // Group 3: Optional flags
    {
        FocusGroup g;
        g.title = " Optional flags "; g.hotkey = '\0';
        g.box_x = 2; g.box_y = FLAGS_BOX_Y;
        g.box_w = W - 4; g.box_h = FLAGS_BOX_H;
        g.text_buffer = &vals_->optional_flags;
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
                .x = W/2 - 24, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    settings_ = temp_;
                    result().accept();
                    return HandleResult::CLOSE;
                }
            },
            Button{
                .label = " &Test ",
                .x = W/2 - 13, .y = BTN_Y,
                .on_activate = [this]() -> HandleResult {
                    runTest();
                    return HandleResult::CONTINUE;
                }
            },
            Button{
                .label = " Cop&y ",
                .x = W/2 - 1, .y = BTN_Y,
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
                .x = W/2 + 11, .y = BTN_Y,
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

std::vector<OptionList::Option> CompileOptionsDialog::options(int tab)
{
    std::vector<OptionList::Option> out;
    if (tab < 0 || tab >= (int)def_.tabs.size()) return out;

    for (const auto& grp : def_.tabs[tab].groups) {
        for (const auto& o : grp.options) {
            int* value = &vals_->values[o.id];
            if (o.radio()) {
                for (int i = 0; i < (int)o.choices.size(); ++i) {
                    OptionList::Option row;
                    row.label     = withFlag(o.choices[i].label, o.choices[i].flag);
                    row.group     = grp.name;
                    row.is_radio  = true;
                    row.radio_val = i;
                    row.i_val     = value;
                    row.disabled  = unsupported_.count(o.id + ":" + std::to_string(i)) > 0;
                    out.push_back(row);
                }
            } else {
                OptionList::Option row;
                row.label    = withFlag(o.label, o.flag);
                row.group    = grp.name;
                row.ic_val   = value;
                row.disabled = unsupported_.count(o.id) > 0;
                out.push_back(row);
            }
        }
    }
    return out;
}

// The command the preview box shows and Copy puts on the clipboard.
std::string CompileOptionsDialog::previewCommand()
{
    if (project_) return buildSystem_.buildProjectPreview(*project_, temp_);
    return buildSystem_.get_full_compile_command(buildSystem_.guessCompileCommand(filename_), temp_);
}

void CompileOptionsDialog::runTest()
{
    // A small progress box while the compiler runs once per option.
    const int bw = 48, bh = 5;
    WINDOW* box_win = newwin(bh, bw, (LINES - bh) / 2, (COLS - bw) / 2);
    auto progress = [&](int done, int total) {
        if (!box_win) return;
        werase(box_win);
        wbkgd(box_win, COLOR_PAIR(Renderer::CP_DIALOG));
        box(box_win, 0, 0);
        const std::string msg = "Testing option " + std::to_string(done) + " of " +
                                std::to_string(total) + "...";
        mvwprintw(box_win, 2, (bw - (int)msg.size()) / 2, "%s", msg.c_str());
        wrefresh(box_win);
    };

    std::set<std::string> rejected;
    std::string error;
    const bool ok = buildSystem_.testOptions(rejected, error, progress);
    if (box_win) delwin(box_win);
    touchwin(stdscr);

    if (!ok) {
        MessageDialog::show(renderer_, error);
        return;
    }
    unsupported_ = rejected;

    int total = 0;
    for (const compilers::Option* o : def_.allOptions())
        total += o->radio() ? (int)std::count_if(o->choices.begin(), o->choices.end(),
                                   [](const compilers::Choice& c) { return !c.flag.empty(); })
                            : (o->flag.empty() ? 0 : 1);
    if (rejected.empty()) {
        MessageDialog::show(renderer_, def_.name + " accepted all " + std::to_string(total) + " options.");
    } else {
        std::string names;
        for (const auto& key : rejected) {
            const std::string id = key.substr(0, key.find(':'));
            const compilers::Option* o = def_.findOption(id);
            std::string what = o ? o->label : id;
            if (o && o->radio()) {
                const int i = std::atoi(key.substr(key.find(':') + 1).c_str());
                if (i >= 0 && i < (int)o->choices.size()) what = o->choices[i].label;
            }
            names += "\n  " + what;
        }
        MessageDialog::show(renderer_, def_.name + " rejected " + std::to_string(rejected.size()) +
                            " of " + std::to_string(total) + " options; they are marked "
                            "\"not supported\" in the list:" + names);
    }
}

void CompileOptionsDialog::onDraw(Renderer& renderer, int startx, int starty)
{
    renderer.drawText(startx + 2, starty + COMBO_Y,
                      def_.top ? def_.top->label + ":" : std::string("C++ Standard:"),
                      Renderer::CP_DIALOG);

    // Sync the top combo into the settings every frame
    if (!groups()[GRP_STANDARD].comboboxes.empty()) {
        const ComboBox& cb = groups()[GRP_STANDARD].comboboxes[0];
        if (def_.top) vals_->values[def_.top->id] = cb.selected_idx;
        else          temp_.cpp_standard = cb.selectedText();
    }

    const int active = groups()[GRP_TABS].tabcontrols[0].activeTab();
    if (!groups()[GRP_OPTIONS].optionlists.empty())
        groups()[GRP_OPTIONS].optionlists[0].options = options(active);
    if (def_.tabs.empty())
        renderer.drawText(startx + 4, starty + OPT_Y + 1,
                          def_.name + " has no options defined; use Optional flags below.",
                          Renderer::CP_DIALOG);

    // Optional flags field content
    renderer.drawText(startx + 4, starty + FLAGS_BOX_Y + 1,
                      std::string(W - 8, ' '), Renderer::CP_LIST_BOX);
    renderer.drawText(startx + 4, starty + FLAGS_BOX_Y + 1,
                      vals_->optional_flags, Renderer::CP_LIST_BOX);

    // wrap_text breaks at spaces only; a long path has none, so split anything
    // still too wide or it runs out over the box's right border.
    // wrap_text also leaves an empty first line when the first word alone is
    // too long (a quoted compiler path), which would waste one of the two rows.
    std::vector<std::string> wrapped;
    for (std::string l : wrap_text(previewCommand(), W - 8)) {
        if (l.empty() && wrapped.empty()) continue;
        while ((int)l.size() > W - 8) { wrapped.push_back(l.substr(0, W - 8)); l.erase(0, W - 8); }
        wrapped.push_back(l);
    }

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

    return HandleResult::CONTINUE;
}

//  Focus order.
std::vector<int> CompileOptionsDialog::tabStops()
{
    return { GRP_STANDARD, GRP_TABS, GRP_OPTIONS,
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
    return true;
}
