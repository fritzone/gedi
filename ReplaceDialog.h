#pragma once
#include "DialogBase.h"
#include "Renderer.h"
#include <string>

class ReplaceDialog : private DialogBase {
public:
    static DialogResult show(Renderer& renderer,
                             const std::string& initial_find    = "",
                             const std::string& initial_replace = "");
private:
    ReplaceDialog(const std::string& initial_find,
                  const std::string& initial_replace);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;

    // FIND + REPLACE inputs + BTN_ROW (all three buttons are one tab stop)
    DeclareCyclicEnum(Focus, FIND, REPLACE, BTN_ROW);

    std::string find_buf_;
    std::string replace_buf_;
    NavigationGraph<Focus> nav_;
};
