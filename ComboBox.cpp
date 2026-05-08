#include "ComboBox.h"
#include <ncurses.h>

ComboBox::ComboBox(const std::vector<std::string>& items, int selected_idx)
    : m_items(items), m_selected_idx(selected_idx) {
    if (m_selected_idx < 0) m_selected_idx = 0;
    if (!m_items.empty() && m_selected_idx >= (int)m_items.size()) m_selected_idx = (int)m_items.size() - 1;
}

void ComboBox::draw(Renderer& renderer, int x, int y, int w, bool has_focus) {
    if (m_items.empty()) return;
    std::string text = m_items[m_selected_idx];
    if (text.length() > (size_t)w - 4) text = text.substr(0, w - 4);
    
    int color = has_focus ? Renderer::CP_MENU_SELECTED : Renderer::CP_LIST_BOX;
    renderer.drawText(x, y, "[ ", Renderer::CP_DIALOG);
    renderer.drawText(x + 2, y, text + std::string(w - 4 - text.length(), ' '), color);
    renderer.drawText(x + w - 2, y, " ]", Renderer::CP_DIALOG);
}

bool ComboBox::handleInput(wint_t ch) {
    if (m_items.empty()) return false;
    if (ch == KEY_LEFT || ch == KEY_UP) {
        if (m_selected_idx > 0) {
            m_selected_idx--;
            return true;
        }
    } else if (ch == KEY_RIGHT || ch == KEY_DOWN || ch == ' ') {
        if (m_selected_idx < (int)m_items.size() - 1) {
            m_selected_idx++;
            return true;
        } else if (ch == ' ' && m_items.size() > 1) {
            m_selected_idx = 0; // Cycle
            return true;
        }
    }
    return false;
}

void ComboBox::setSelectedIndex(int idx) {
    if (idx >= 0 && idx < (int)m_items.size()) {
        m_selected_idx = idx;
    }
}
