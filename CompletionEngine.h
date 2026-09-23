#pragma once
#include <string>
#include <vector>
#include <mutex>

// A single code-completion candidate produced by libclang.
struct CompletionItem {
    std::string insert;    // the text actually inserted (the typed-text chunk)
    std::string display;   // human-readable signature, e.g. "size() : size_type"
    int         priority = 0;   // libclang priority (lower = better)
};

// Wraps libclang code completion. Keeps one translation unit alive between calls
// (reparsing it with the live buffer contents) so repeated completions on the same
// file are fast after the first parse. Not thread-safe: the owner must serialise
// calls (gedi runs at most one completion at a time, on a worker thread).
class CompletionEngine {
public:
    CompletionEngine();
    ~CompletionEngine();

    CompletionEngine(const CompletionEngine&) = delete;
    CompletionEngine& operator=(const CompletionEngine&) = delete;

    // Parse (or reparse the cached TU) for abs_path using the unsaved `content`
    // and the given compiler `args`, then return the completions available at the
    // 1-based (line, col). Returns an empty list on failure.
    std::vector<CompletionItem> complete(const std::string& abs_path,
                                         const std::string& content,
                                         const std::vector<std::string>& args,
                                         int line, int col);

    // Drop the cached translation unit (e.g. when the active file changes).
    void invalidate();

private:
    // Serialises complete()/invalidate(): a cancelled completion's worker thread
    // may still be parsing when the next one starts, and they share m_tu.
    std::mutex  m_mutex;
    void* m_index = nullptr;   // CXIndex (opaque here to keep clang out of the header)
    void* m_tu    = nullptr;   // CXTranslationUnit
    std::string m_path;        // path the cached TU was built for
};
