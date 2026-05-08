#ifndef TABCONTROL_H
#define TABCONTROL_H

#include <vector>
#include <string>
#include "Renderer.h"

class TabControl {
public:
    TabControl(const std::vector<std::string>& tab_names, int active_tab = 0);
    void draw(Renderer& renderer, int x, int y, int w, bool has_focus);
    bool handleInput(wint_t ch); // Returns true if active tab changed
    int getActiveTab() const { return m_active_tab; }
    void setActiveTab(int tab);

private:
    std::vector<std::string> m_tab_names;
    int m_active_tab;
};

#endif // TABCONTROL_H
