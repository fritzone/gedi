#pragma once
#include "DialogBase.h"
#include <string>
#include <vector>

// Search > Quick Open: type part of a file's name, pick it from the list.
struct QuickOpenItem {
    std::string rel;            // shown and matched: relative to the project root, or absolute
    std::string abs;            // what gets opened
    bool        open = false;   // already in a window
    std::string detail;         // Symbols: shown after the name ("function  ns::Cls")
};

class QuickOpenDialog : private DialogBase {
public:
    // `items` in the order to list them before anything is typed. `query` is
    // the text to start with, and holds the last one typed afterwards.
    // Returns the index of the chosen item (-1: cancelled); `line` is the line
    // asked for with "name:line", or 0.
    // Files: `rel` is a path - the name is shown first, "name:line" picks a line.
    // Symbols (Go to Symbol): `rel` is the name, `detail` follows it.
    enum class Mode { Files, Symbols };
    static int show(Renderer& renderer, const std::vector<QuickOpenItem>& items,
                    std::string& query, int& line, Mode mode = Mode::Files);

private:
    QuickOpenDialog(const std::vector<QuickOpenItem>& items, const std::string& query, int w, int h, Mode mode);

    void onInit() override;
    void onDraw(Renderer& renderer, int startx, int starty) override;
    HandleResult onKey(wint_t ch) override;
    bool onPlaceCursor(Renderer& renderer, int sx, int sy) override;
    bool onMouseClick(const MEVENT& ev, int startx, int starty) override;

    void refilter();                       // when the query changed
    void moveCursor(int delta);

    // The query split into the name pattern and an optional ":line".
    void splitQuery(std::string& pattern, int& line) const;

    static constexpr int FIELD_X = 3;
    static constexpr int FIELD_Y = 2;
    static constexpr int LIST_Y  = 4;
    static constexpr int MAX_SHOWN = 2000;   // matches kept after sorting

    int w_, h_;
    int listRows() const { return h_ - LIST_Y - 4; }
    int fieldW() const { return w_ - 6; }

    Mode mode_;
    const std::vector<QuickOpenItem>& items_;
    std::string query_;
    std::string filtered_for_ = "\x01";   // the pattern `matches_` was made for
    struct Match { int idx; std::vector<int> pos; };
    std::vector<Match> matches_;
    int cursor_ = 0, top_ = 0;
    int chosen_ = -1;
};
