#ifndef EDITORBUFFER_H
#define EDITORBUFFER_H

#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>
#include "CompilerSettings.h"

// A single definition location extracted from a parsed TU.
struct SymbolDef {
    std::string file;   // absolute path
    unsigned    line = 0;
    unsigned    col  = 0;
};

// Per-buffer libclang semantic highlight cache.
// The background ClangHighlighter thread writes colors and the definition map
// into this struct; the rendering thread reads colors lock-free via the
// shared_ptr snapshot; GoToDefinition reads the definition map under mutex.
struct SemanticCache {
    std::mutex                                           mutex;
    std::shared_ptr<std::vector<std::vector<uint8_t>>>   colors; // [line0][col0] = CP_* (0 = regex fallback)
    // Symbol name → all definition locations visible in this TU (non-system only).
    // Populated by ClangHighlighter; read by GoToDefinition for instant lookup.
    std::unordered_map<std::string, std::vector<SymbolDef>> definition_map;
    std::atomic<bool>  dirty{true};
    std::atomic<bool>  in_progress{false};
    std::atomic<int>   version{0};
};

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
    // Snapshot of line content at last save/load; used to detect real changes.
    std::vector<std::string> saved_lines;
    CompilerSettings compiler_settings;
    std::shared_ptr<SemanticCache> semantic_cache{std::make_shared<SemanticCache>()};

};

#endif // EDITORBUFFER_H
