#pragma once
#include "DialogBase.h"
#include <string>
#include <vector>

// Search > Find in Files: what to look for, how, and where.
struct FindInFilesOptions {
    std::string text;
    bool match_case = false;
    bool whole_word = false;
    bool regex      = false;
    int  scope      = 0;      // index into the `scopes` passed to show()
};

class FindInFilesDialog : private DialogBase {
public:
    // `scopes`: the places that can be searched, as (label, one-line note).
    // Returns true when the user pressed Find; `opts` holds the choices then.
    static bool show(Renderer& renderer, FindInFilesOptions& opts,
                     const std::vector<std::pair<std::string, std::string>>& scopes);

private:
    FindInFilesDialog(const FindInFilesOptions& opts,
                      const std::vector<std::pair<std::string, std::string>>& scopes);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;
    HandleResult onKey(wint_t ch) override;
    bool onPlaceCursor(Renderer& renderer, int sx, int sy) override;
    bool onKeyFirst(wint_t ch) override;

    //  Layout (dialog-relative)
    static constexpr int W        = 64;
    static constexpr int H        = 15;
    static constexpr int FIELD_X  = 3;
    static constexpr int FIELD_Y  = 3;
    static constexpr int FIELD_W  = W - 6;
    static constexpr int BOX_Y    = 5;
    static constexpr int BOX_H    = 5;
    static constexpr int OPT_X    = 2;
    static constexpr int OPT_W    = 26;
    static constexpr int WHERE_X  = OPT_X + OPT_W + 1;
    static constexpr int WHERE_W  = W - WHERE_X - 2;
    static constexpr int NOTE_Y   = BOX_Y + BOX_H;
    static constexpr int BTN_Y    = H - 3;

    static constexpr int GRP_TEXT  = 0;
    static constexpr int GRP_OPTS  = 1;
    static constexpr int GRP_WHERE = 2;

    FindInFilesOptions opts_;
    std::vector<std::pair<std::string, std::string>> scopes_;
    int scope_cursor_ = 0;
    // The text it opened with counts as selected: the first key typed replaces it.
    bool fresh_ = false;
};
