#pragma once
#include "DialogBase.h"
#include <string>

struct AddFileInfo {
    bool        is_new   = true;
    std::string filepath;
};

class AddFileDialog : private DialogBase {
public:
    static bool show(Renderer& renderer, const std::string& project_dir, AddFileInfo& out);

private:
    AddFileDialog(Renderer& renderer, const std::string& project_dir, AddFileInfo& out);
    void onInit() override;
    void onDraw(Renderer& renderer, int sx, int sy) override;
    HandleResult onKey(wint_t ch) override;
    bool onPlaceCursor(Renderer& renderer, int sx, int sy) override;
    bool onTab(bool forward) override;

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int W            = 56;
    static constexpr int H            = 13;
    static constexpr int TYPE_BOX_Y   = 1;
    static constexpr int TYPE_BOX_H   = 3;
    static constexpr int FILE_BOX_Y   = 4;
    static constexpr int FILE_BOX_H   = 4;
    static constexpr int FIELD_X      = 3;      // from inner_x (startx+2)
    static constexpr int FIELD_W      = 37;
    static constexpr int BROWSE_BTN_X = 2 + FIELD_X + FIELD_W + 1;  // = 43
    static constexpr int BROWSE_BTN_Y = FILE_BOX_Y + 1;              // = 5
    static constexpr int BTN_ADD_X    = 18;
    static constexpr int BTN_CANCEL_X = BTN_ADD_X + 6 + 4;           // = 28
    static constexpr int BTN_Y        = H - 3;                        // = 10

    static constexpr int GRP_TYPE = 0;
    static constexpr int GRP_FILE = 1;
    static constexpr int BTN_IDX_ADD    = 0;
    static constexpr int BTN_IDX_CANCEL = 1;
    static constexpr int BTN_IDX_BROWSE = 2;

    Renderer&    renderer_;
    std::string  project_dir_;
    AddFileInfo& info_;
    int          type_cursor_ = 0;  // 0 = New, 1 = Existing
    int          file_cursor_ = 0;
    int          file_scroll_ = 0;
};
