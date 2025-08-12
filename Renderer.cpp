#include "nlohmann/json.hpp"
#include "Renderer.h"

#include <ncurses.h>
#include <termios.h>

#include <fstream>

Renderer::Renderer() {
    setlocale(LC_ALL, ""); initscr();

    cbreak(); noecho();
    keypad(stdscr, TRUE); nodelay(stdscr, TRUE); curs_set(1);
    start_color(); use_default_colors();
    getmaxyx(stdscr, m_height, m_width);
    struct termios term;
    if (tcgetattr(STDIN_FILENO, &term) == 0) {
        term.c_iflag &= ~(IXON | IXOFF);
        tcsetattr(STDIN_FILENO, TCSANOW, &term);
    }
    m_color_map = {
        {"black", COLOR_BLACK}, {"red", COLOR_RED}, {"green", COLOR_GREEN},
        {"yellow", COLOR_YELLOW}, {"blue", COLOR_BLUE}, {"magenta", COLOR_MAGENTA},
        {"cyan", COLOR_CYAN}, {"white", COLOR_WHITE},
        {"brightblack", COLOR_BLACK + 8}, {"brightred", COLOR_RED + 8},
        {"brightgreen", COLOR_GREEN + 8}, {"brightyellow", COLOR_YELLOW + 8},
        {"brightblue", COLOR_BLUE + 8}, {"brightmagenta", COLOR_MAGENTA + 8},
        {"brightcyan", COLOR_CYAN + 8}, {"brightwhite", COLOR_WHITE + 8}
    };

    m_color_pair_map = {
        {"default", CP_DEFAULT_TEXT}, {"highlight", CP_HIGHLIGHT}, {"menu_bar", CP_MENU_BAR},
        {"menu_item", CP_MENU_ITEM}, {"menu_selected", CP_MENU_SELECTED}, {"dialog", CP_DIALOG},
        {"dialog_button", CP_DIALOG_BUTTON}, {"selection", CP_SELECTION}, {"status_bar", CP_STATUS_BAR},
        {"status_bar_highlight", CP_STATUS_BAR_HIGHLIGHT}, {"shadow", CP_SHADOW}, {"dialog_title", CP_DIALOG_TITLE},
        {"changed_indicator", CP_CHANGED_INDICATOR}, {"list_box", CP_LIST_BOX},
        {"keyword", CP_SYNTAX_KEYWORD}, {"comment", CP_SYNTAX_COMMENT},
        {"string", CP_SYNTAX_STRING}, {"number", CP_SYNTAX_NUMBER}, {"preprocessor", CP_SYNTAX_PREPROCESSOR},
        {"register_variable", CP_SYNTAX_REGISTER_VAR},
        // Add new mappings for gutter and buttons
        {"gutter_bg", CP_GUTTER_BG}, {"gutter_fg", CP_GUTTER_FG},
        {"button_bg", CP_BUTTON_BG}, {"button_text", CP_BUTTON_TEXT},
        {"button_hotkey", CP_BUTTON_HOTKEY}, {"button_selected_bg", CP_BUTTON_SELECTED_BG},
        {"button_selected_text", CP_BUTTON_SELECTED_TEXT}, {"button_selected_hotkey", CP_BUTTON_SELECTED_HOTKEY},
        {"button_shadow", CP_BUTTON_SHADOW}
    };
}

Renderer::~Renderer() { curs_set(1); endwin(); }

void Renderer::clear() { werase(stdscr); }

void Renderer::refresh() { wrefresh(stdscr); }

void Renderer::updateDimensions() { getmaxyx(stdscr, m_height, m_width); }

void Renderer::drawText(int x, int y, const std::string &text, int colorId, int flags) {
    wattron(stdscr, COLOR_PAIR(colorId));
    if (flags & A_BOLD) wattron(stdscr, A_BOLD);
    if (flags & A_UNDERLINE) wattron(stdscr, A_UNDERLINE);
    mvwaddstr(stdscr, y, x, text.c_str());
    if (flags & A_UNDERLINE) wattroff(stdscr, A_UNDERLINE);
    if (flags & A_BOLD) wattroff(stdscr, A_BOLD);
    wattroff(stdscr, COLOR_PAIR(colorId));
}

void Renderer::drawStyledText(int x, int y, const std::string &text, int colorId) {
    wattron(stdscr, COLOR_PAIR(colorId));
    wmove(stdscr, y, x);
    for (size_t i = 0; i < text.length(); ++i) {
        if (text[i] == '&' && i + 1 < text.length()) {
            i++;
            wattron(stdscr, A_UNDERLINE);
            waddch(stdscr, text[i]);
            wattroff(stdscr, A_UNDERLINE);
        } else { waddch(stdscr, text[i]); }
    }
    wattroff(stdscr, COLOR_PAIR(colorId));
}

void Renderer::drawBox(int x, int y, int w, int h, int colorId, BoxStyle style) {
    if (w < 2 || h < 2) return;
    if (style == SINGLE) {
        wattron(stdscr, COLOR_PAIR(colorId));
        mvaddch(y, x, ACS_ULCORNER); mvaddch(y, x + w - 1, ACS_URCORNER);
        mvaddch(y + h - 1, x, ACS_LLCORNER); mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
        mvhline(y, x + 1, ACS_HLINE, w - 2); mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
        mvvline(y + 1, x, ACS_VLINE, h - 2); mvvline(y + 1, x + w - 1, ACS_VLINE, h - 2);
        wattroff(stdscr, COLOR_PAIR(colorId));
    } else {
        cchar_t tl, tr, bl, br, hline, vline;
        setcchar(&tl, L"╔", WA_NORMAL, colorId, NULL); setcchar(&tr, L"╗", WA_NORMAL, colorId, NULL);
        setcchar(&bl, L"╚", WA_NORMAL, colorId, NULL); setcchar(&br, L"╝", WA_NORMAL, colorId, NULL);
        setcchar(&hline, L"═", WA_NORMAL, colorId, NULL); setcchar(&vline, L"║", WA_NORMAL, colorId, NULL);
        mvwadd_wch(stdscr, y, x, &tl); mvwadd_wch(stdscr, y, x + w - 1, &tr);
        mvwadd_wch(stdscr, y + h - 1, x, &bl); mvwadd_wch(stdscr, y + h - 1, x + w - 1, &br);
        mvwhline_set(stdscr, y, x + 1, &hline, w - 2); mvwhline_set(stdscr, y + h - 1, x + 1, &hline, w - 2);
        mvwvline_set(stdscr, y + 1, x, &vline, h - 2); mvwvline_set(stdscr, y + 1, x + w - 1, &vline, h - 2);
    }
}

void Renderer::drawBoxWithTitle(int x, int y, int w, int h, int colorId, BoxStyle style, const std::string &title, int title_color, int title_flags) {
    drawBox(x, y, w, h, colorId, style);
    if (!title.empty()) {
        std::string spaced_title = " " + title + " ";

        // Calculate the visible length of the title (ignoring '&') to center it correctly
        size_t visible_len = 0;
        for (size_t i = 0; i < spaced_title.length(); ++i) {
            if (spaced_title[i] == '&' && i + 1 < spaced_title.length()) {
                i++; // Skip the ampersand itself, the next char is the hotkey
            }
            visible_len++;
        }

        if (visible_len < (size_t)w - 2) {
            int start_x = x + (w - visible_len) / 2;

            // Draw the title with hotkey processing
            wattron(stdscr, COLOR_PAIR(title_color));
            if (title_flags & A_BOLD) wattron(stdscr, A_BOLD);

            wmove(stdscr, y, start_x);
            for (size_t i = 0; i < spaced_title.length(); ++i) {
                if (spaced_title[i] == '&' && i + 1 < spaced_title.length()) {
                    i++;
                    wattron(stdscr, A_UNDERLINE);
                    waddch(stdscr, spaced_title[i]);
                    wattroff(stdscr, A_UNDERLINE);
                } else {
                    waddch(stdscr, spaced_title[i]);
                }
            }

            if (title_flags & A_BOLD) wattroff(stdscr, A_BOLD);
            wattroff(stdscr, COLOR_PAIR(title_color));
        }
    }
}

void Renderer::drawShadow(int x, int y, int w, int h) {
    cchar_t underlying_char, shadow_char;
    wchar_t char_buffer[2] = {0};
    for (int row = y + 1; row < y + h + 1; ++row) {
        if (row < m_height && (x + w) < m_width) {
            mvin_wch(row, x + w, &underlying_char);
            char_buffer[0] = underlying_char.chars[0]; if (char_buffer[0] == 0) { char_buffer[0] = L' '; }
            setcchar(&shadow_char, char_buffer, A_NORMAL, CP_SHADOW, NULL); mvadd_wch(row, x + w, &shadow_char);
        }
    }

    for (int col = x + 1; col < x + w + 1; ++col) {
        if ((y + h) < m_height && col < m_width) {
            mvin_wch(y + h, col, &underlying_char);
            char_buffer[0] = underlying_char.chars[0]; if (char_buffer[0] == 0) { char_buffer[0] = L' '; }
            setcchar(&shadow_char, char_buffer, A_NORMAL, CP_SHADOW, NULL); mvadd_wch(y + h, col, &shadow_char);
        }
    }
}

wint_t Renderer::getChar() { wint_t ch; int result = wget_wch(stdscr, &ch); return (result == ERR) ? ERR : ch; }

void Renderer::hideCursor() { curs_set(0); }

void Renderer::showCursor() { curs_set(1); }

int Renderer::getWidth() const { return m_width; }

int Renderer::getHeight() const { return m_height; }

void Renderer::setCursor(int x, int y) { move(y, x); }

int Renderer::getStyleFlags(ColorPairID id) const {
    if (m_style_attributes.count(id)) {
        return m_style_attributes.at(id);
    }
    return 0;
}

void Renderer::loadColors(const json &theme_data) {
    m_style_attributes.clear();

    auto init_colors_from_json = [&](const json& j) {
        for (auto const& [key, val] : j.items()) {
            if (m_color_pair_map.count(key)) {
                int pair_id = m_color_pair_map[key];
                init_pair(pair_id, m_color_map[val["fg"]], m_color_map[val["bg"]]);
                if (val.contains("bold") && val["bold"].get<bool>()) {
                    m_style_attributes[static_cast<ColorPairID>(pair_id)] = A_BOLD;
                }
            }
        }
    };

    if (theme_data.contains("ui")) init_colors_from_json(theme_data["ui"]);
    if (theme_data.contains("syntax")) init_colors_from_json(theme_data["syntax"]);

    short dialog_fg, dialog_bg;
    pair_content(CP_DIALOG, &dialog_fg, &dialog_bg);
    short default_fg, default_bg;
    pair_content(CP_DEFAULT_TEXT, &default_fg, &default_bg);
    short sel_fg, sel_bg;
    pair_content(CP_SELECTION, &sel_fg, &sel_bg);

    init_pair(CP_COMPILE_ERROR, COLOR_RED, dialog_bg);
    init_pair(CP_COMPILE_WARNING, COLOR_YELLOW, dialog_bg);
    init_pair(CP_DEFAULT_ON_SELECTION, default_fg, sel_bg);
}


void Renderer::drawButton(int x, int y, const std::string& text, bool selected) {
    cchar_t upper_shadow, lower_shadow;
    setcchar(&upper_shadow, L"▀", WA_NORMAL, CP_BUTTON_SHADOW, NULL);
    setcchar(&lower_shadow, L"▄", WA_NORMAL, CP_BUTTON_SHADOW, NULL);

    for (size_t i = 0; i < text.length(); ++i) {
        mvwadd_wch(stdscr, y + 1, x + 1 + i, &upper_shadow);
    }
    mvwadd_wch(stdscr, y, x + text.length(), &lower_shadow);

    int bg_color = selected ? CP_BUTTON_SELECTED_BG : CP_BUTTON_BG;
    int text_color = selected ? CP_BUTTON_SELECTED_TEXT : CP_BUTTON_TEXT;
    int hotkey_color = selected ? CP_BUTTON_SELECTED_HOTKEY : CP_BUTTON_HOTKEY;

    wattron(stdscr, COLOR_PAIR(bg_color));
    mvwaddstr(stdscr, y, x, std::string(text.length(), ' ').c_str());
    wattroff(stdscr, COLOR_PAIR(bg_color));

    wmove(stdscr, y, x);
    for (size_t i = 0; i < text.length(); ++i) {
        if (text[i] == '&' && i + 1 < text.length()) {
            i++;
            wattron(stdscr, COLOR_PAIR(hotkey_color) | A_BOLD);
            waddch(stdscr, text[i]);
            wattroff(stdscr, COLOR_PAIR(hotkey_color) | A_BOLD);
        } else {
            wattron(stdscr, COLOR_PAIR(text_color));
            waddch(stdscr, text[i]);
            wattroff(stdscr, COLOR_PAIR(text_color));
        }
    }
}

void Renderer::createDefaultColorsFile() {
    json j;
    j["ui"] = {
        {"default", {{"fg", "white"}, {"bg", "blue"}}},
        {"highlight", {{"fg", "black"}, {"bg", "cyan"}}},
        {"menu_bar", {{"fg", "black"}, {"bg", "white"}}},
        {"menu_item", {{"fg", "black"}, {"bg", "white"}}},
        {"menu_selected", {{"fg", "white"}, {"bg", "cyan"}}},
        {"dialog", {{"fg", "black"}, {"bg", "white"}}},
        {"dialog_button", {{"fg", "white"}, {"bg", "blue"}}},
        {"dialog_title", {{"fg", "red"}, {"bg", "white"}, {"bold", true}}},
        {"selection", {{"fg", "black"}, {"bg", "yellow"}}},
        {"status_bar", {{"fg", "black"}, {"bg", "white"}}},
        {"status_bar_highlight", {{"fg", "red"}, {"bg", "white"}}},
        {"shadow", {{"fg", "white"}, {"bg", "black"}}},
        {"changed_indicator", {{"fg", "green"}, {"bg", "blue"}, {"bold", true}}}
    };
    j["syntax"] = {
        {"keyword", {{"fg", "white"}, {"bg", "blue"}, {"bold", true}}},
        {"comment", {{"fg", "green"}, {"bg", "blue"}}},
        {"string", {{"fg", "red"}, {"bg", "blue"}}},
        {"number", {{"fg", "red"}, {"bg", "blue"}}},
        {"preprocessor", {{"fg", "cyan"}, {"bg", "blue"}}},
        {"register_variable", {{"fg", "yellow"}, {"bg", "blue"}}}
    };
    std::ofstream o("/usr/share/gedi/colors.json");
    o << std::setw(4) << j << std::endl;
}
