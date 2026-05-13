#pragma once
#include "DialogBase.h"
#include "Renderer.h"

class GoToLineDialog : private DialogBase {
public:
    static int show(Renderer& renderer, int current_line, int max_lines);

private:
    GoToLineDialog(int current_line, int max_lines);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;

    // INPUTFIELD + BTN_ROW (the whole button row is one tab stop)
    DeclareCyclicEnum(Focus, INPUTFIELD, BTN_ROW);

    int         max_lines_;
    std::string line_buf_;
    NavigationGraph<Focus> nav_;
};
