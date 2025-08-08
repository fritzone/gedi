#ifndef UNDORECORD_H
#define UNDORECORD_H

#include <vector>
#include <string>

struct UndoRecord {
    std::vector<std::string> lines;
    int cursor_line_num;
    int cursor_col;
    int first_visible_line_num;
};

#endif // UNDORECORD_H
