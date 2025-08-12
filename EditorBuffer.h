#ifndef EDITORBUFFER_H
#define EDITORBUFFER_H

#include <vector>
#include <string>
#include <map>

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

struct EditorBuffer {
    Line *document_head = nullptr;
    int total_lines = 0;
    std::string filename;
    bool changed = false;
    bool is_new_file = false;
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
    std::vector<UndoRecord> undo_stack;
    std::vector<UndoRecord> redo_stack;
    enum SyntaxType { NONE, C_CPP, MAKEFILE, CMAKE, ASSEMBLY, LD_SCRIPT, GLSL };
    SyntaxType syntax_type = NONE;
    std::map<std::string, int> keywords;
    bool in_multiline_comment = false;
    
    EditorBuffer() {
        document_head = new Line();
        total_lines = 1;
        current_line = document_head;
        first_visible_line = document_head;
        filename = "new_file.txt";
        is_new_file = true;
    }
    
    ~EditorBuffer() {
        Line* p = document_head;
        while (p != nullptr) { Line* q = p; p = p->next; delete q; }
    }
    
    EditorBuffer(const EditorBuffer& other) :
        total_lines(other.total_lines), filename(other.filename), changed(other.changed),
        is_new_file(other.is_new_file), insert_mode(other.insert_mode),
        cursor_col(other.cursor_col), current_line_num(other.current_line_num),
        cursor_screen_y(other.cursor_screen_y), horizontal_scroll_offset(other.horizontal_scroll_offset),
        selecting(other.selecting), selection_anchor_col(other.selection_anchor_col),
        selection_anchor_linenum(other.selection_anchor_linenum), syntax_type(other.syntax_type),
        keywords(other.keywords), in_multiline_comment(other.in_multiline_comment)
    {
        if (!other.document_head) {
            document_head = nullptr; current_line = nullptr; first_visible_line = nullptr; selection_anchor_line = nullptr;
            return;
        }
        document_head = new Line{other.document_head->text};
        Line* this_curr = document_head;
        const Line* other_curr = other.document_head;
        if (other_curr == other.current_line) current_line = this_curr;
        if (other_curr == other.first_visible_line) first_visible_line = this_curr;
        if (other_curr == other.selection_anchor_line) selection_anchor_line = this_curr;
        other_curr = other_curr->next;
        while(other_curr) {
            this_curr->next = new Line{other_curr->text, this_curr};
            this_curr = this_curr->next;
            if (other_curr == other.current_line) current_line = this_curr;
            if (other_curr == other.first_visible_line) first_visible_line = this_curr;
            if (other_curr == other.selection_anchor_line) selection_anchor_line = this_curr;
            other_curr = other_curr->next;
        }
    }
    
    EditorBuffer& operator=(const EditorBuffer& other) {
        if (this == &other) return *this;
        EditorBuffer temp(other);
        std::swap(*this, temp);
        return *this;
    }
    
    EditorBuffer(EditorBuffer&& other) noexcept :
        document_head(other.document_head), total_lines(other.total_lines),
        filename(std::move(other.filename)), changed(other.changed),
        is_new_file(other.is_new_file), insert_mode(other.insert_mode),
        current_line(other.current_line), first_visible_line(other.first_visible_line),
        cursor_col(other.cursor_col), current_line_num(other.current_line_num),
        cursor_screen_y(other.cursor_screen_y), horizontal_scroll_offset(other.horizontal_scroll_offset),
        selecting(other.selecting), selection_anchor_line(other.selection_anchor_line),
        selection_anchor_col(other.selection_anchor_col), selection_anchor_linenum(other.selection_anchor_linenum),
        undo_stack(std::move(other.undo_stack)), redo_stack(std::move(other.redo_stack)),
        syntax_type(other.syntax_type), keywords(std::move(other.keywords)),
        in_multiline_comment(other.in_multiline_comment)
    {
        other.document_head = nullptr;
    }
    
    EditorBuffer& operator=(EditorBuffer&& other) noexcept {
        if (this == &other) return *this;
        Line* p = document_head;
        while (p != nullptr) { Line* q = p; p = p->next; delete q; }
        document_head = other.document_head; total_lines = other.total_lines;
        filename = std::move(other.filename); changed = other.changed;
        is_new_file = other.is_new_file; insert_mode = other.insert_mode;
        current_line = other.current_line; first_visible_line = other.first_visible_line;
        cursor_col = other.cursor_col; current_line_num = other.current_line_num;
        cursor_screen_y = other.cursor_screen_y; horizontal_scroll_offset = other.horizontal_scroll_offset;
        selecting = other.selecting; selection_anchor_line = other.selection_anchor_line;
        selection_anchor_col = other.selection_anchor_col; selection_anchor_linenum = other.selection_anchor_linenum;
        undo_stack = std::move(other.undo_stack); redo_stack = std::move(other.redo_stack);
        syntax_type = other.syntax_type; keywords = std::move(other.keywords);
        in_multiline_comment = other.in_multiline_comment;
        other.document_head = nullptr;
        return *this;
    }
};

#endif // EDITORBUFFER_H
