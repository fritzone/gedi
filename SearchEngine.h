#ifndef SEARCHENGINE_H
#define SEARCHENGINE_H

#include <string>
#include "EditorBuffer.h"

struct SearchResult {
    Line* line = nullptr;
    int line_num = -1;
    int col = -1;
    bool found = false;
};

struct MatchStats {
    int total   = 0;
    int current = 0;  // 1-based index of the current match; 0 if not found
};

class SearchEngine {
public:
    static SearchResult search(EditorBuffer& buffer, const std::string& term, Line* startLine, int startLineNum, int startCol, bool forward = true);
    static int replaceAll(EditorBuffer& buffer, const std::string& searchTerm, const std::string& replaceTerm);
    static MatchStats getMatchStats(EditorBuffer& buffer, const std::string& term, int current_line_num, int current_col);
};

#endif // SEARCHENGINE_H
