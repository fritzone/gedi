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

class SearchEngine {
public:
    static SearchResult search(EditorBuffer& buffer, const std::string& term, Line* startLine, int startLineNum, int startCol, bool forward = true);
    static int replaceAll(EditorBuffer& buffer, const std::string& searchTerm, const std::string& replaceTerm);
};

#endif // SEARCHENGINE_H
