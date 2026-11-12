#include "ClangOutline.h"

#include <clang-c/Index.h>
#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_map>

namespace outline {

static std::string str(CXString s)
{
    const char* c = clang_getCString(s);
    std::string r = c ? c : "";
    clang_disposeString(s);
    return r;
}

// The kinds an outline lists, and what it calls them; "" = not listed.
static const char* kindName(CXCursor c)
{
    switch (clang_getCursorKind(c)) {
    case CXCursor_Namespace:              return "namespace";
    case CXCursor_ClassDecl:              return "class";
    case CXCursor_ClassTemplate:          return "class";
    case CXCursor_StructDecl:             return "struct";
    case CXCursor_UnionDecl:              return "union";
    case CXCursor_EnumDecl:               return "enum";
    case CXCursor_FunctionDecl:           return "function";
    case CXCursor_FunctionTemplate:       return "function";
    case CXCursor_CXXMethod:              return "method";
    case CXCursor_Constructor:            return "constructor";
    case CXCursor_Destructor:             return "destructor";
    case CXCursor_ConversionFunction:     return "operator";
    case CXCursor_FieldDecl:              return "field";
    case CXCursor_TypedefDecl:            return "type";
    case CXCursor_TypeAliasDecl:          return "type";
    case CXCursor_TypeAliasTemplateDecl:  return "type";
    // (macros come from the text - fromText: a parse with a precompiled
    //  preamble does not report the ones at the top of the file)
    case CXCursor_VarDecl: {
        // Globals and static members - not a function's locals.
        const CXCursorKind p = clang_getCursorKind(clang_getCursorSemanticParent(c));
        return (p == CXCursor_TranslationUnit || p == CXCursor_Namespace ||
                p == CXCursor_ClassDecl || p == CXCursor_StructDecl ||
                p == CXCursor_ClassTemplate || p == CXCursor_UnionDecl) ? "variable" : "";
    }
    default: return "";
    }
}

static bool isFunctionLike(CXCursorKind k)
{
    return k == CXCursor_FunctionDecl || k == CXCursor_FunctionTemplate || k == CXCursor_CXXMethod ||
           k == CXCursor_Constructor || k == CXCursor_Destructor || k == CXCursor_ConversionFunction;
}

// "ns::Cls" - the namespaces and classes around a declaration.
static std::string scopeOf(CXCursor c)
{
    std::string scope;
    for (CXCursor p = clang_getCursorSemanticParent(c);
         !clang_Cursor_isNull(p) && clang_getCursorKind(p) != CXCursor_TranslationUnit &&
         !clang_isInvalid(clang_getCursorKind(p));
         p = clang_getCursorSemanticParent(p)) {
        std::string n = str(clang_getCursorSpelling(p));
        if (n.empty()) n = "(anonymous)";
        scope = scope.empty() ? n : n + "::" + scope;
    }
    return scope;
}

namespace {
struct Walk {
    std::vector<OutlineSymbol> out;
    std::unordered_map<std::string, size_t> by_usr;   // USR -> index in out
    std::vector<bool> is_def;                         // per out entry
};
}

std::vector<OutlineSymbol> fromTranslationUnit(void* tu_ptr)
{
    CXTranslationUnit tu = static_cast<CXTranslationUnit>(tu_ptr);
    Walk w;
    if (!tu) return w.out;

    clang_visitChildren(clang_getTranslationUnitCursor(tu),
        [](CXCursor c, CXCursor, CXClientData data) -> CXChildVisitResult {
            Walk& w = *static_cast<Walk*>(data);
            // Only what the file itself declares; whatever its headers hold is skipped whole.
            if (!clang_Location_isFromMainFile(clang_getCursorLocation(c))) return CXChildVisit_Continue;

            const CXCursorKind k = clang_getCursorKind(c);
            const char* kind = kindName(c);
            if (*kind) {
                std::string name = str(clang_getCursorSpelling(c));
                if (!name.empty()) {
                    unsigned line = 0, col = 0;
                    clang_getSpellingLocation(clang_getCursorLocation(c), nullptr, &line, &col, nullptr);
                    const bool def = clang_isCursorDefinition(c) || k == CXCursor_MacroDefinition;
                    const std::string usr = str(clang_getCursorUSR(c));
                    OutlineSymbol s{name, kind, k == CXCursor_MacroDefinition ? std::string() : scopeOf(c), line, col};

                    // Declared and defined here: one entry, at the definition. A
                    // parse that skipped function bodies does not report them as
                    // definitions - then the last one, as a definition follows its
                    // prototype.
                    auto it = usr.empty() ? w.by_usr.end() : w.by_usr.find(usr);
                    if (it != w.by_usr.end()) {
                        if (!w.is_def[it->second]) { w.out[it->second] = s; w.is_def[it->second] = def; }
                    } else {
                        if (!usr.empty()) w.by_usr[usr] = w.out.size();
                        w.out.push_back(std::move(s));
                        w.is_def.push_back(def);
                    }
                }
            }
            // Into namespaces, classes, extern "C" ...; not into function bodies or initialisers.
            if (isFunctionLike(k) || k == CXCursor_VarDecl || k == CXCursor_FieldDecl ||
                k == CXCursor_EnumDecl || k == CXCursor_MacroDefinition)
                return CXChildVisit_Continue;
            return CXChildVisit_Recurse;
        },
        &w);

    std::stable_sort(w.out.begin(), w.out.end(),
                     [](const OutlineSymbol& a, const OutlineSymbol& b) {
                         return a.line != b.line ? a.line < b.line : a.col < b.col;
                     });
    return w.out;
}

bool parse(const std::string& file, const std::string& content,
           const std::vector<std::string>& args,
           std::vector<OutlineSymbol>& out, std::string& error)
{
    std::vector<const char*> argv;
    argv.reserve(args.size());
    for (const auto& a : args) argv.push_back(a.c_str());

    CXUnsavedFile uf;
    uf.Filename = file.c_str();
    uf.Contents = content.c_str();
    uf.Length   = content.size();

    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit tu = clang_parseTranslationUnit(
        index, file.c_str(), argv.data(), (int)argv.size(), &uf, 1,
        CXTranslationUnit_SkipFunctionBodies |            // not needed: much quicker
        CXTranslationUnit_Incomplete |
        CXTranslationUnit_KeepGoing);
    if (!tu) {
        error = "The file could not be parsed.";
        clang_disposeIndex(index);
        return false;
    }
    out = fromTranslationUnit(tu);
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    return true;
}

std::vector<OutlineSymbol> fromText(const std::vector<std::string>& lines, EditorBuffer::SyntaxType type)
{
    std::vector<OutlineSymbol> out;
    if (type == EditorBuffer::ST_MAKEFILE) {
        // "target other: deps" (not "VAR := x" / "VAR ::= x"), and .PHONY-like specials skipped.
        static const std::regex rule(R"(^([^\s:#=][^:#=]*?)\s*::?(?![=])(.*)$)");
        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string& ln = lines[i];
            if (ln.empty() || ln[0] == '\t' || ln[0] == '#') continue;
            std::smatch m;
            if (!std::regex_match(ln, m, rule)) continue;
            const std::string names = m[1].str();
            if (m[2].str().rfind(":=", 0) == 0) continue;   // "VAR ::= x"
            size_t pos = 0;
            while (pos < names.size()) {
                size_t s = names.find_first_not_of(" \t", pos);
                if (s == std::string::npos) break;
                size_t e = names.find_first_of(" \t", s);
                if (e == std::string::npos) e = names.size();
                const std::string t = names.substr(s, e - s);
                if (t != "&" && (t[0] != '.' || t.find('/') != std::string::npos))   // "a b &:" grouped targets
                    out.push_back({t, t.find('%') != std::string::npos ? "pattern" : "target", "",
                                   (unsigned)i + 1, (unsigned)s + 1});
                pos = e;
            }
        }
    } else if (type == EditorBuffer::ST_ASSEMBLY) {
        // "label:" at the start of a line (GNU as, NASM), not local numeric labels.
        static const std::regex label(R"(^\s*([A-Za-z_.$][\w.$]*)\s*:)");
        for (size_t i = 0; i < lines.size(); ++i) {
            std::smatch m;
            if (std::regex_search(lines[i], m, label))
                out.push_back({m[1].str(), "label", "", (unsigned)i + 1, (unsigned)m.position(1) + 1});
        }
    } else if (type == EditorBuffer::ST_C_CPP) {
        // The macros: "#define NAME" (also "#  define", inside #if blocks too).
        static const std::regex def(R"(^\s*#\s*define\s+([A-Za-z_]\w*))");
        for (size_t i = 0; i < lines.size(); ++i) {
            std::smatch m;
            if (std::regex_search(lines[i], m, def))
                out.push_back({m[1].str(), "macro", "", (unsigned)i + 1, (unsigned)m.position(1) + 1});
        }
    } else if (type == EditorBuffer::ST_CMAKE) {
        static const std::regex fn(R"(^\s*(function|macro)\s*\(\s*([\w.+-]+))", std::regex::icase);
        static const std::regex tgt(R"(^\s*add_(executable|library)\s*\(\s*([\w.+-]+))", std::regex::icase);
        for (size_t i = 0; i < lines.size(); ++i) {
            std::smatch m;
            if (std::regex_search(lines[i], m, fn))
                out.push_back({m[2].str(), "function", "", (unsigned)i + 1, (unsigned)m.position(2) + 1});
            else if (std::regex_search(lines[i], m, tgt))
                out.push_back({m[2].str(), "target", "", (unsigned)i + 1, (unsigned)m.position(2) + 1});
        }
    }
    return out;
}

} // namespace outline
