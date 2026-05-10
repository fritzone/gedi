#include "CompileOptionsDialog.h"
#include "ComboBox.h"
#include "TabControl.h"
#include "utils.h"
#include <ncurses.h>
#include <algorithm>
#include <vector>

void CompileOptionsDialog::show(Renderer& renderer, BuildSystem& buildSystem, EditorBuffer& buffer) {
    int h = 34, w = 84;
    int starty = (renderer.getHeight() - h) / 2;
    int startx = (renderer.getWidth() - w) / 2;

    WINDOW* behind = newwin(h + 1, w + 1, starty, startx);
    copywin(stdscr, behind, starty, startx, 0, 0, h, w, FALSE);

    CompilerSettings& settings = buffer.compiler_settings;
    CompilerSettings temp_settings = settings;

    std::vector<std::string> standards = {"c++11", "c++14", "c++17", "c++20", "c++23", "c++26"};
    int std_idx = 0;
    for(size_t i=0; i<standards.size(); ++i) if(standards[i] == temp_settings.cpp_standard) std_idx = i;
    ComboBox stdCombo(standards, std_idx);

    TabControl tabs({"Basic", "Advanced", "Expert"});
    int focus_area = 0; // 0: Standard, 1: Tabs, 2: Content, 3: Optional, 4: Command, 5: Buttons
    int sub_focus = 0;
    int top_opt = 0;
    int top_cmd_row = 0;

    struct Option {
        std::string label;
        std::string group;
        bool is_radio;
        int radio_val;
        bool* b_val;
        int* i_val;
    };

    auto get_options = [&](int tab_idx) {
        std::vector<Option> opts;
        if (tab_idx == 0) {
            opts = {
                {"Generate debug symbols (-g)", "Optimization", false, 0, &temp_settings.debug_symbols, nullptr},
                {"Disable optimisation (-O0)", "Optimization", true, 0, nullptr, &temp_settings.optimization_level},
                {"Balanced optimisation (-O2)", "Optimization", true, 1, nullptr, &temp_settings.optimization_level},
                {"High optimisation (-O3)", "Optimization", true, 2, nullptr, &temp_settings.optimization_level},
                {"Enable common warnings (-Wall)", "Warnings", false, 0, &temp_settings.wall, nullptr},
                {"Enable extra warnings (-Wextra)", "Warnings", false, 0, &temp_settings.wextra, nullptr},
                {"Strict ISO compliance (-Wpedantic)", "Warnings", false, 0, &temp_settings.wpedantic, nullptr},
                {"Treat warnings as errors (-Werror)", "Warnings", false, 0, &temp_settings.werror, nullptr}
            };
        } else if (tab_idx == 1) {
            opts = {
                {"Warn on implicit conversions (-Wconversion)", "Warnings", false, 0, &temp_settings.wconversion, nullptr},
                {"Warn on signed/unsigned conv (-Wsign-conversion)", "Warnings", false, 0, &temp_settings.wsign_conversion, nullptr},
                {"Warn on variable shadowing (-Wshadow)", "Warnings", false, 0, &temp_settings.wshadow, nullptr},
                {"Warn on non-virtual destructors (-Wnon-virtual-dtor)", "Warnings", false, 0, &temp_settings.wnon_virtual_dtor, nullptr},
                {"Warn on old-style casts (-Wold-style-cast)", "Warnings", false, 0, &temp_settings.wold_style_cast, nullptr},
                {"Warn on overloaded virtuals (-Woverloaded-virtual)", "Warnings", false, 0, &temp_settings.woverloaded_virtual, nullptr},
                {"Warn on null dereference (-Wnull-dereference)", "Warnings", false, 0, &temp_settings.wnull_dereference, nullptr},
                {"Warn on double promotion (-Wdouble-promotion)", "Warnings", false, 0, &temp_settings.wdouble_promotion, nullptr},
                {"Strict format string checking (-Wformat=2)", "Warnings", false, 0, &temp_settings.wformat_2, nullptr},
                {"Keep frame pointer (-fno-omit-frame-pointer)", "Sanitizer", false, 0, &temp_settings.fno_omit_frame_pointer, nullptr},
                {"Enable ASan and UBSan (-fsanitize=address,ub)", "Sanitizer", false, 0, &temp_settings.fsanitize_address_ub, nullptr},
                {"Enable LeakSanitizer (-fsanitize=leak)", "Sanitizer", false, 0, &temp_settings.fsanitize_leak, nullptr},
                {"Enable LTO (-flto)", "Sanitizer", false, 0, &temp_settings.flto, nullptr},
                {"Optimise for host CPU (-march=native)", "Optimizer", false, 0, &temp_settings.march_native, nullptr},
                {"Tune for host CPU (-mtune=native)", "Optimizer", false, 0, &temp_settings.mtune_native, nullptr}
            };
        } else {
            opts = {
                {"Warn on cast alignment (-Wcast-align)", "Warnings", false, 0, &temp_settings.wcast_align, nullptr},
                {"Warn on cast qualifiers (-Wcast-qual)", "Warnings", false, 0, &temp_settings.wcast_qual, nullptr},
                {"Warn on missing enum cases (-Wswitch-enum)", "Warnings", false, 0, &temp_settings.wswitch_enum, nullptr},
                {"Warn on undefined macros (-Wundef)", "Warnings", false, 0, &temp_settings.wundef, nullptr},
                {"Warn on redundant decls (-Wredundant-decls)", "Warnings", false, 0, &temp_settings.wredundant_decls, nullptr},
                {"Warn on logical op issues (-Wlogical-op)", "Warnings", false, 0, &temp_settings.wlogical_op, nullptr},
                {"Warn on useless casts (-Wuseless-cast)", "Warnings", false, 0, &temp_settings.wuseless_cast, nullptr},
                {"Effective C++ warnings (-Weffc++)", "Warnings", false, 0, &temp_settings.weffcxx, nullptr},
                {"Disable exceptions (-fno-exceptions)", "Functionality", false, 0, &temp_settings.fno_exceptions, nullptr},
                {"Disable RTTI (-fno-rtti)", "Functionality", false, 0, &temp_settings.fno_rtti, nullptr},
                {"Hide symbols by default (-fvisibility=hidden)", "Functionality", false, 0, &temp_settings.fvisibility_hidden, nullptr},
                {"Enable strict aliasing (-fstrict-aliasing)", "Functionality", false, 0, &temp_settings.fstrict_aliasing, nullptr},
                {"Sanitise ptr comparisons (-fsanitize=ptr-cmp)", "Sanitizer", false, 0, &temp_settings.fsanitize_pointer_compare, nullptr},
                {"Sanitise ptr arithmetic (-fsanitize=ptr-sub)", "Sanitizer", false, 0, &temp_settings.fsanitize_pointer_subtract, nullptr},
                {"Linker: remove unused deps (--as-needed)", "Linker", false, 0, &temp_settings.wl_as_needed, nullptr},
                {"Linker: optimise linking (-Wl,-O1)", "Linker", false, 0, &temp_settings.wl_o1, nullptr}
            };
        }
        return opts;
    };

    std::string base_cmd = buildSystem.guessCompileCommand(buffer.filename);

    nodelay(stdscr, FALSE);
    bool dialog_active = true;
    int last_tab = -1;
    bool pressed = false;

    while (dialog_active) {
        if (tabs.getActiveTab() != last_tab) {
            top_opt = 0; sub_focus = 0;
            last_tab = tabs.getActiveTab();
        }

        renderer.drawShadow(startx, starty, w, h);
        renderer.drawBoxWithTitle(startx, starty, w, h, Renderer::CP_DIALOG, Renderer::DOUBLE, " Compiler Options ", Renderer::CP_DIALOG_TITLE, A_BOLD);

        wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
        for (int i = 1; i < h - 1; ++i) mvwaddstr(stdscr, starty + i, startx + 1, std::string(w - 2, ' ').c_str());
        wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));

        renderer.drawText(startx + 2, starty + 2, "C++ Standard:", Renderer::CP_DIALOG);
        stdCombo.draw(renderer, startx + 17, starty + 2, 12, focus_area == 0);

        tabs.draw(renderer, startx + 2, starty + 4, w - 4, focus_area == 1);

        auto current_opts = get_options(tabs.getActiveTab());
        int visible_opt_rows = h - 20;
        int opt_y = starty + 6;

        struct Row { bool is_group; std::string text; int opt_idx; };
        std::vector<Row> rows;
        std::string last_group;
        for (int i = 0; i < (int)current_opts.size(); ++i) {
            if (current_opts[i].group != last_group) {
                last_group = current_opts[i].group;
                rows.push_back({true, "- Group: " + last_group, -1});
            }
            bool val = current_opts[i].is_radio ? (*current_opts[i].i_val == current_opts[i].radio_val) : *current_opts[i].b_val;
            std::string mark = current_opts[i].is_radio ? (val ? "(•)" : "( )") : (val ? "[X]" : "[ ]");
            rows.push_back({false, mark + " " + current_opts[i].label, i});
        }

        int selected_row = -1;
        for(int i=0; i<(int)rows.size(); ++i) if(!rows[i].is_group && rows[i].opt_idx == sub_focus) selected_row = i;
        if (selected_row < top_opt) top_opt = selected_row;
        if (selected_row >= top_opt + visible_opt_rows) top_opt = selected_row - visible_opt_rows + 1;

        for (int i = 0; i < visible_opt_rows && (top_opt + i) < (int)rows.size(); ++i) {
            const auto& row = rows[top_opt + i];
            if (row.is_group) renderer.drawText(startx + 2, opt_y + i, row.text, Renderer::CP_DIALOG, A_BOLD);
            else {
                int color = (focus_area == 2 && row.opt_idx == sub_focus) ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG;
                renderer.drawText(startx + 4, opt_y + i, row.text, color);
            }
        }

        renderer.drawBoxWithTitle(startx + 2, starty + h - 13, w - 4, 3, Renderer::CP_DIALOG, Renderer::SINGLE, " Optional flags ", Renderer::CP_DIALOG, (focus_area == 3 ? A_BOLD : 0));
        renderer.drawText(startx + 4, starty + h - 12, std::string(w - 8, ' '), Renderer::CP_LIST_BOX);
        renderer.drawText(startx + 4, starty + h - 12, temp_settings.optional_flags, Renderer::CP_LIST_BOX);

        std::string final_cmd = buildSystem.get_full_compile_command(base_cmd, temp_settings);
        std::vector<std::string> wrapped = wrap_text(final_cmd, w - 8);
        renderer.drawBoxWithTitle(startx + 2, starty + h - 10, w - 4, 4, Renderer::CP_DIALOG, Renderer::SINGLE, " Final Command ", Renderer::CP_DIALOG, (focus_area == 4 ? A_BOLD : 0));
        
        int cmd_view_h = 2;
        if (top_cmd_row < 0) top_cmd_row = 0;
        if (top_cmd_row > (int)wrapped.size() - cmd_view_h) top_cmd_row = (int)wrapped.size() - cmd_view_h;
        if (top_cmd_row < 0) top_cmd_row = 0;

        for (int i = 0; i < cmd_view_h; ++i) {
            if (top_cmd_row + i < (int)wrapped.size()) {
                int color = (focus_area == 4) ? Renderer::CP_MENU_SELECTED : Renderer::CP_DIALOG;
                renderer.drawText(startx + 4, starty + h - 9 + i, wrapped[top_cmd_row + i], color);
            }
        }

        renderer.drawButton(startx + w/2 - 20, starty + h - 4, " &Ok ", focus_area == 5 && sub_focus == 0, pressed && focus_area == 5 && sub_focus == 0);
        renderer.drawButton(startx + w/2 - 6, starty + h - 4, " Cop&y ", focus_area == 5 && sub_focus == 1, pressed && focus_area == 5 && sub_focus == 1);
        renderer.drawButton(startx + w/2 + 8, starty + h - 4, " &Cancel ", focus_area == 5 && sub_focus == 2, pressed && focus_area == 5 && sub_focus == 2);

        if (focus_area == 3) { renderer.showCursor(); move(starty + h - 12, startx + 4 + temp_settings.optional_flags.length()); }
        else renderer.hideCursor();
        renderer.refresh();

        if (pressed) {
            napms(100);
            if (focus_area == 5 && sub_focus == 1) { // Copy
                FILE* p = popen("xclip -selection clipboard -i", "w");
                if (p) { fputs(final_cmd.c_str(), p); pclose(p); }
                pressed = false; continue;
            }
            break;
        }

        wint_t ch = renderer.getChar();
        if (ch == 27) {
            timeout(1); wint_t nch = renderer.getChar(); timeout(-1);
            if (nch == ERR) { dialog_active = false; break; }
            switch(tolower(nch)) {
                case 'o': focus_area = 5; sub_focus = 0; pressed = true; break;
                case 'y': focus_area = 5; sub_focus = 1; pressed = true; break;
                case 'c': focus_area = 5; sub_focus = 2; pressed = true; break;
            }
        } else {
            switch(ch) {
                case 9: focus_area = (focus_area + 1) % 6; sub_focus = 0; break;
                case KEY_BTAB: focus_area = (focus_area + 5) % 6; sub_focus = 0; break;
                case KEY_UP:
                    if (focus_area == 2 && sub_focus > 0) sub_focus--;
                    else if (focus_area == 4 && top_cmd_row > 0) top_cmd_row--;
                    else if (focus_area > 0) { 
                        focus_area--; 
                        if (focus_area == 2) sub_focus = get_options(tabs.getActiveTab()).size() - 1; 
                        else sub_focus = 0; 
                    }
                    break;
                case KEY_DOWN:
                    if (focus_area == 2 && sub_focus < (int)get_options(tabs.getActiveTab()).size() - 1) sub_focus++;
                    else if (focus_area == 4 && top_cmd_row < (int)wrapped.size() - cmd_view_h) top_cmd_row++;
                    else if (focus_area < 5) { focus_area++; sub_focus = 0; }
                    break;
                case KEY_LEFT:
                    if (focus_area == 0) { stdCombo.handleInput(ch); temp_settings.cpp_standard = stdCombo.getSelectedText(); }
                    else if (focus_area == 1) tabs.handleInput(ch);
                    else if (focus_area == 5 && sub_focus > 0) sub_focus--;
                    break;
                case KEY_RIGHT:
                    if (focus_area == 0) { stdCombo.handleInput(ch); temp_settings.cpp_standard = stdCombo.getSelectedText(); }
                    else if (focus_area == 1) tabs.handleInput(ch);
                    else if (focus_area == 5 && sub_focus < 2) sub_focus++;
                    break;
                case ' ': case KEY_ENTER: case 10: case 13:
                    if (focus_area == 2) {
                        auto current = get_options(tabs.getActiveTab());
                        if (current[sub_focus].is_radio) *current[sub_focus].i_val = current[sub_focus].radio_val;
                        else *current[sub_focus].b_val = !*current[sub_focus].b_val;
                    } else if (focus_area == 5) {
                        if (sub_focus == 0) { settings = temp_settings; }
                        pressed = true;
                    }
                    break;
                case KEY_BACKSPACE: case 127: case 8:
                    if (focus_area == 3 && !temp_settings.optional_flags.empty()) temp_settings.optional_flags.pop_back();
                    break;
                default:
                    if (focus_area == 3 && ch > 31 && ch < KEY_MIN) temp_settings.optional_flags += wchar_to_utf8(ch);
                    break;
            }
        }
    }

    copywin(behind, stdscr, 0, 0, starty, startx, h, w, FALSE); delwin(behind);
    nodelay(stdscr, TRUE);
    renderer.showCursor();
}
