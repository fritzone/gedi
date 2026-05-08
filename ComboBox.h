#ifndef COMBOBOX_H
#define COMBOBOX_H

#include <vector>
#include <string>
#include "Renderer.h"

class ComboBox {
public:
    ComboBox(const std::vector<std::string>& items, int selected_idx = 0);
    void draw(Renderer& renderer, int x, int y, int w, bool has_focus);
    bool handleInput(wint_t ch); // Returns true if selection changed
    int getSelectedIndex() const { return m_selected_idx; }
    std::string getSelectedText() const { return m_items[m_selected_idx]; }
    void setSelectedIndex(int idx);

private:
    std::vector<std::string> m_items;
    int m_selected_idx;
};

#endif // COMBOBOX_H
