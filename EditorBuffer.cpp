#include "EditorBuffer.h"

EditorBuffer::EditorBuffer(const EditorBuffer &other) :
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

EditorBuffer::EditorBuffer(int nr) : bufferNr(nr) {
    document_head = new Line();

    current_line = document_head;
    first_visible_line = document_head;
}

EditorBuffer::~EditorBuffer() {
    Line* p = document_head;
    while (p != nullptr) { Line* q = p; p = p->next; delete q; }
}

EditorBuffer &EditorBuffer::operator=(const EditorBuffer &other) {
    if (this == &other) return *this;
    EditorBuffer temp(other);
    std::swap(*this, temp);
    return *this;
}

EditorBuffer::EditorBuffer(EditorBuffer &&other) noexcept :
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

EditorBuffer &EditorBuffer::operator=(EditorBuffer &&other) noexcept {
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
