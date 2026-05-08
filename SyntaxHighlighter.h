#ifndef SYNTAXHIGHLIGHTER_H
#define SYNTAXHIGHLIGHTER_H

#include "EditorBuffer.h"
#include "Renderer.h"
#include <vector>
#include <string>

struct SyntaxToken {
    std::string text;
    int colorId;
    int flags = 0;
};

class SyntaxHighlighter {
public:
    static void setSyntaxType(EditorBuffer& buffer);
    static void loadKeywords(EditorBuffer& buffer);
    static std::vector<SyntaxToken> parseLine(EditorBuffer& buffer, const std::string& line, const Renderer& renderer);
};

#endif
