#ifndef LINE_H
#define LINE_H

#include <string>

struct Line {
    std::string text;
    Line* prev = nullptr;
    Line* next = nullptr;
    bool selected = false;
    int selection_start_col = 0;
    int selection_end_col = 0;
};

#endif // LINE_H
