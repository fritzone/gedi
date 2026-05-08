#include "SearchEngine.h"
#include <algorithm>

SearchResult SearchEngine::search(EditorBuffer& buffer, const std::string& term, Line* startLine, int startLineNum, int startCol, bool forward) {
    SearchResult result;
    if (term.empty()) return result;

    std::string lower_search_term = term;
    std::transform(lower_search_term.begin(), lower_search_term.end(), lower_search_term.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    Line* p = startLine;
    int current_line_num = startLineNum;
    int col = startCol;
    int lines_searched = 0;

    while (lines_searched <= buffer.total_lines) {
        std::string lower_text = p->text;
        std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        size_t found_pos;
        if (forward) {
            found_pos = lower_text.find(lower_search_term, col);
        } else {
            // Backward search implementation (simplified)
            // find_last_of is not exactly what we want for substrings
            // We'll use rfind for backward search
            found_pos = lower_text.rfind(lower_search_term, col > 0 ? col - 1 : std::string::npos);
        }

        if (found_pos != std::string::npos) {
            result.line = p;
            result.line_num = current_line_num;
            result.col = (int)found_pos + 1;
            result.found = true;
            return result;
        }

        if (forward) {
            p = p->next;
            current_line_num++;
            if (p == nullptr) {
                p = buffer.document_head;
                current_line_num = 1;
            }
            col = 0;
        } else {
            p = p->prev;
            current_line_num--;
            if (p == nullptr) {
                p = buffer.document_head;
                // Go to last line
                while (p->next) { p = p->next; current_line_num++; }
            }
            col = p->text.length();
        }
        lines_searched++;
    }

    return result;
}

int SearchEngine::replaceAll(EditorBuffer& buffer, const std::string& searchTerm, const std::string& replaceTerm) {
    if (searchTerm.empty()) return 0;

    int replacements = 0;
    std::string lower_search = searchTerm;
    std::transform(lower_search.begin(), lower_search.end(), lower_search.begin(), ::tolower);

    for (Line* p = buffer.document_head; p != nullptr; p = p->next) {
        std::string lower_line = p->text;
        std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);

        size_t pos = lower_line.find(lower_search);
        while (pos != std::string::npos) {
            p->text.replace(pos, searchTerm.length(), replaceTerm);
            replacements++;

            lower_line = p->text;
            std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);
            pos = lower_line.find(lower_search, pos + replaceTerm.length());
        }
    }
    return replacements;
}
