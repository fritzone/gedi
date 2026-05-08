#include "TabControl.h"
#include <ncurses.h>

TabControl::TabControl(const std::vector<std::string>& tab_names, int active_tab)
    : m_tab_names(tab_names), m_active_tab(active_tab) {
    if (m_active_tab < 0) m_active_tab = 0;
    if (!m_tab_names.empty() && m_active_tab >= (int)m_tab_names.size()) m_active_tab = (int)m_tab_names.size() - 1;
}

void TabControl::draw(Renderer& renderer, int x, int y, int w, bool has_focus) {
    if (m_tab_names.empty()) return;

    int current_x = x;
    for (int i = 0; i < (int)m_tab_names.size(); ++i) {
        std::string tab_text = " " + m_tab_names[i] + " ";
        int color;
        int style = 0;
        
        if (i == m_active_tab) {
            color = has_focus ? Renderer::CP_MENU_SELECTED : Renderer::CP_HIGHLIGHT;
            style = A_BOLD;
        } else {
            color = Renderer::CP_DIALOG;
        }
        
        renderer.drawText(current_x, y, tab_text, color, style);
        current_x += tab_text.length() + 1;
    }
    
    // Draw a separator line
    wattron(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
    mvwhline(stdscr, y + 1, x, ACS_HLINE, w);
    wattroff(stdscr, COLOR_PAIR(Renderer::CP_DIALOG));
}

bool TabControl::handleInput(wint_t ch) {
    if (m_tab_names.empty()) return false;
    if (ch == KEY_LEFT) {
        if (m_active_tab > 0) {
            m_active_tab--;
            return true;
        }
    } else if (ch == KEY_RIGHT) {
        if (m_active_tab < (int)m_tab_names.size() - 1) {
            m_active_tab++;
            return true;
        }
    }
    return false;
}

void TabControl::setActiveTab(int tab) {
    if (tab >= 0 && tab < (int)m_tab_names.size()) {
        m_active_tab = tab;
    }
}
