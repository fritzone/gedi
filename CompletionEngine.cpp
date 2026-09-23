#include "CompletionEngine.h"

#include <clang-c/Index.h>
#include <algorithm>
#include <cctype>

CompletionEngine::CompletionEngine() {
    m_index = clang_createIndex(0, 0);
}

CompletionEngine::~CompletionEngine() {
    if (m_tu)    clang_disposeTranslationUnit(static_cast<CXTranslationUnit>(m_tu));
    if (m_index) clang_disposeIndex(static_cast<CXIndex>(m_index));
}

void CompletionEngine::invalidate() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_tu) {
        clang_disposeTranslationUnit(static_cast<CXTranslationUnit>(m_tu));
        m_tu = nullptr;
    }
    m_path.clear();
}

// Options that make repeated completion fast: build a precompiled preamble and
// cache completion results across reparses.
static const unsigned kParseOpts =
    CXTranslationUnit_PrecompiledPreamble |
    CXTranslationUnit_CacheCompletionResults |
    CXTranslationUnit_DetailedPreprocessingRecord |
    CXTranslationUnit_KeepGoing;

std::vector<CompletionItem> CompletionEngine::complete(const std::string& abs_path,
                                                       const std::string& content,
                                                       const std::vector<std::string>& args,
                                                       int line, int col) {
    std::vector<CompletionItem> out;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_index) return out;

    std::vector<const char*> cargs;
    cargs.reserve(args.size());
    for (const auto& s : args) cargs.push_back(s.c_str());

    CXUnsavedFile uf;
    uf.Filename = abs_path.c_str();
    uf.Contents = content.c_str();
    uf.Length   = static_cast<unsigned long>(content.size());

    auto index = static_cast<CXIndex>(m_index);
    auto tu    = static_cast<CXTranslationUnit>(m_tu);

    // Reuse the cached TU for the same file (fast reparse); otherwise parse fresh.
    if (tu && m_path == abs_path) {
        if (clang_reparseTranslationUnit(tu, 1, &uf,
                                         clang_defaultReparseOptions(tu)) != 0) {
            clang_disposeTranslationUnit(tu);
            tu = nullptr;
        }
    } else if (tu) {
        clang_disposeTranslationUnit(tu);
        tu = nullptr;
    }

    if (!tu) {
        tu = clang_parseTranslationUnit(index, abs_path.c_str(),
                                        cargs.data(), static_cast<int>(cargs.size()),
                                        &uf, 1, kParseOpts);
        m_path = abs_path;
    }
    m_tu = tu;
    if (!tu) { m_path.clear(); return out; }

    CXCodeCompleteResults* res = clang_codeCompleteAt(
        tu, abs_path.c_str(), static_cast<unsigned>(line), static_cast<unsigned>(col),
        &uf, 1, clang_defaultCodeCompleteOptions());
    if (!res) return out;

    clang_sortCodeCompletionResults(res->Results, res->NumResults);

    out.reserve(res->NumResults);
    for (unsigned i = 0; i < res->NumResults; ++i) {
        const CXCompletionResult& r = res->Results[i];
        CXCompletionString cs = r.CompletionString;

        if (clang_getCompletionAvailability(cs) == CXAvailability_NotAvailable)
            continue;

        std::string insert;      // the typed-text chunk (what we actually insert)
        std::string sig;         // inline signature (name + parameters)
        std::string result_type; // return / result type, shown as a suffix

        unsigned n = clang_getNumCompletionChunks(cs);
        for (unsigned c = 0; c < n; ++c) {
            CXCompletionChunkKind ck = clang_getCompletionChunkKind(cs, c);
            CXString cxs = clang_getCompletionChunkText(cs, c);
            std::string t = clang_getCString(cxs) ? clang_getCString(cxs) : "";
            clang_disposeString(cxs);

            if (ck == CXCompletionChunk_ResultType) { result_type = t; continue; }
            if (ck == CXCompletionChunk_TypedText)  insert = t;
            sig += t;
        }

        if (insert.empty()) continue;   // skip anything we can't insert cleanly

        CompletionItem item;
        item.insert   = std::move(insert);
        item.display  = result_type.empty() ? sig : sig + " : " + result_type;
        item.priority = static_cast<int>(clang_getCompletionPriority(cs));
        out.push_back(std::move(item));
    }

    clang_disposeCodeCompleteResults(res);

    // Stable order: priority first, then alphabetical by inserted text.
    std::sort(out.begin(), out.end(), [](const CompletionItem& a, const CompletionItem& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        if (a.insert   != b.insert)   return a.insert   < b.insert;
        return a.display < b.display;
    });
    // Drop exact duplicates (same insert + display) that overloads/using can create.
    out.erase(std::unique(out.begin(), out.end(),
                          [](const CompletionItem& a, const CompletionItem& b) {
                              return a.insert == b.insert && a.display == b.display;
                          }),
              out.end());
    return out;
}
