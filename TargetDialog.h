#pragma once
#include "DialogBase.h"
#include "GediProject.h"
#include <string>

class TargetDialog : private DialogBase {
public:
    // is_new=true  → title "Add Target",        name pre-cleared
    // is_new=false → title "Target Properties", fields pre-filled from target
    // Returns true if the user pressed Ok; target is updated in that case.
    static bool show(Renderer& renderer, ProjectTarget& target, bool is_new);

private:
    TargetDialog(const ProjectTarget& initial, bool is_new);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;

    // ── Layout ────────────────────────────────────────────────────────────────
    static constexpr int W            = 50;
    static constexpr int H            = 14;
    static constexpr int NAME_FIELD_X = 3;
    static constexpr int NAME_FIELD_Y = 3;
    static constexpr int NAME_FIELD_W = W - 6;
    static constexpr int TYPE_BOX_X   = 2;
    static constexpr int TYPE_BOX_Y   = 5;
    static constexpr int TYPE_BOX_W   = W - 4;
    static constexpr int TYPE_BOX_H   = 5;
    static constexpr int BTN_Y        = 11;

    static constexpr int GRP_NAME = 0;
    static constexpr int GRP_TYPE = 1;

    bool        is_new_;
    std::string name_;
    int         type_sel_ = 0;   // 0=executable, 1=static_library, 2=shared_library
    int         type_cur_ = 0;
};
