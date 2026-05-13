#ifndef EDITORBUFFER_H
#define EDITORBUFFER_H

#include <vector>
#include <string>
#include <map>
#include "CompilerSettings.h"

struct Line {
    std::string text;
    Line* prev = nullptr;
    Line* next = nullptr;
    bool selected = false;
    int selection_start_col = 0;
    int selection_end_col = 0;
};

struct UndoRecord {
    std::vector<std::string> lines;
    int cursor_line_num;
    int cursor_col;
    int first_visible_line_num;
};

struct EditorBuffer final {

    enum SyntaxType { ST_NONE, ST_C_CPP, ST_MAKEFILE, ST_CMAKE, ST_ASSEMBLY, ST_LD_SCRIPT, ST_GLSL, PRIMAL };

public:

    EditorBuffer(int nr);

    ~EditorBuffer();

    EditorBuffer(const EditorBuffer& other);

    EditorBuffer& operator=(const EditorBuffer& other);

    EditorBuffer(EditorBuffer&& other) noexcept;

    EditorBuffer& operator=(EditorBuffer&& other) noexcept;

public:
    Line *document_head = nullptr;
    int total_lines = 1;
    std::string filename{"noname00.cpp"};
    bool changed = false;
    bool is_new_file = true;
    bool read_only = false;
    bool insert_mode = true;
    Line *current_line = nullptr;
    Line *first_visible_line = nullptr;
    int cursor_col = 1;
    int current_line_num = 1;
    int cursor_screen_y = 0;
    int horizontal_scroll_offset = 1;
    bool selecting = false;
    Line* selection_anchor_line = nullptr;
    int selection_anchor_col = 1;
    int selection_anchor_linenum = 1;
    SyntaxType syntax_type = ST_NONE;
    std::map<std::string, int> keywords;
    bool in_multiline_comment = false;
    int bufferNr = 1;
    std::vector<UndoRecord> undo_stack;
    std::vector<UndoRecord> redo_stack;
    CompilerSettings compiler_settings;

};

#endif // EDITORBUFFER_H
