#pragma once
#include "DialogBase.h"
#include "GediProject.h"
#include <string>
#include <vector>

class PickTargetDialog : private DialogBase {
public:
    // Returns the index into 'targets' of the chosen target, or -1 if cancelled.
    static int show(Renderer& renderer,
                    const std::vector<ProjectTarget>& targets,
                    const std::string& dialog_title,
                    int exclude_idx = -1);

private:
    PickTargetDialog(const std::vector<ProjectTarget>& targets,
                     const std::string& dialog_title,
                     int exclude_idx);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;
    HandleResult onKey(wint_t ch) override;

    static constexpr int W          = 50;
    static constexpr int H          = 14;
    static constexpr int LIST_BOX_X = 1;
    static constexpr int LIST_BOX_Y = 1;
    static constexpr int LIST_BOX_W = W - 2;
    static constexpr int LIST_BOX_H = H - 5;   // 9 rows; 7 visible inside
    static constexpr int VISIBLE    = LIST_BOX_H - 2;
    static constexpr int BTN_Y      = H - 3;
    static constexpr int GRP_LIST   = 0;

    std::vector<std::string> items_;    // display strings
    std::vector<int>         real_idx_; // maps items_ index → targets index
    int cursor_ = 0;
    int scroll_ = 0;
};
