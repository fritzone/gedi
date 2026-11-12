#pragma once
// A file's outline - its functions, classes, globals, macros - for Search >
// Go to Symbol. C and C++ through libclang; makefiles and assembly by their text.
#include "EditorBuffer.h"
#include <string>
#include <vector>

namespace outline {

// The outline of a translation unit's main file (`tu` is a CXTranslationUnit),
// in line order. A symbol declared and defined in the file is listed once, at
// its definition.
std::vector<OutlineSymbol> fromTranslationUnit(void* tu);

// Parses `file` (with the unsaved `content`) on its own - function bodies
// skipped, so it is quick - and returns its outline. False when it cannot be
// parsed (`error` says why).
bool parse(const std::string& file, const std::string& content,
           const std::vector<std::string>& args,
           std::vector<OutlineSymbol>& out, std::string& error);

// Makefile targets / assembly labels / CMake functions and targets / C and C++
// macros, from the lines of the file.
std::vector<OutlineSymbol> fromText(const std::vector<std::string>& lines, EditorBuffer::SyntaxType type);

} // namespace outline
