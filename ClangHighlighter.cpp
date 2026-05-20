#include "ClangHighlighter.h"
#include "BuildSystem.h"
#include "Renderer.h"
#include "utils.h"

#include <clang-c/Index.h>
#include <thread>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

// ── cursor-kind → ColorPairID ─────────────────────────────────────────────────

static int kindToColor(CXCursorKind k)
{
    switch (k) {
    case CXCursor_StructDecl:
    case CXCursor_UnionDecl:
    case CXCursor_ClassDecl:
    case CXCursor_EnumDecl:
    case CXCursor_TypedefDecl:
    case CXCursor_ClassTemplate:
    case CXCursor_ClassTemplatePartialSpecialization:
    case CXCursor_TypeRef:
    case CXCursor_TemplateRef:
    case CXCursor_TemplateTypeParameter:
    case CXCursor_NonTypeTemplateParameter:
        return Renderer::CP_SYNTAX_TYPE;

    case CXCursor_FunctionDecl:
    case CXCursor_CXXMethod:
    case CXCursor_Constructor:
    case CXCursor_Destructor:
    case CXCursor_ConversionFunction:
    case CXCursor_FunctionTemplate:
        return Renderer::CP_SYNTAX_FUNCTION;

    case CXCursor_ParmDecl:
        return Renderer::CP_SYNTAX_PARAMETER;

    case CXCursor_FieldDecl:
        return Renderer::CP_SYNTAX_FIELD;

    case CXCursor_Namespace:
    case CXCursor_NamespaceRef:
    case CXCursor_NamespaceAlias:
    case CXCursor_UsingDirective:
    case CXCursor_UsingDeclaration:
        return Renderer::CP_SYNTAX_NAMESPACE;

    case CXCursor_EnumConstantDecl:
        return Renderer::CP_SYNTAX_ENUM_CONSTANT;

    case CXCursor_MacroDefinition:
    case CXCursor_MacroExpansion:
        return Renderer::CP_SYNTAX_MACRO;

    case CXCursor_InclusionDirective:
        return Renderer::CP_SYNTAX_PREPROCESSOR;

    default:
        return 0; // let regex fallback handle it
    }
}

static int colorForToken(CXTokenKind tokKind, CXCursor cursor, const std::string& text)
{
    if (tokKind == CXToken_Keyword)     return Renderer::CP_SYNTAX_KEYWORD;
    if (tokKind == CXToken_Comment)     return Renderer::CP_SYNTAX_COMMENT;
    if (tokKind == CXToken_Literal) {
        if (!text.empty() && (text[0] == '"' || text[0] == '\''))
            return Renderer::CP_SYNTAX_STRING;
        return Renderer::CP_SYNTAX_NUMBER;
    }
    if (tokKind == CXToken_Punctuation) return 0;

    // CXToken_Identifier — resolve semantic meaning
    CXCursorKind ck = clang_getCursorKind(cursor);

    // For reference / call expressions, follow to the declaration
    if (ck == CXCursor_DeclRefExpr  || ck == CXCursor_MemberRefExpr ||
        ck == CXCursor_CallExpr     || ck == CXCursor_OverloadedDeclRef) {
        CXCursor ref = clang_getCursorReferenced(cursor);
        if (!clang_Cursor_isNull(ref))
            ck = clang_getCursorKind(ref);
    }

    return kindToColor(ck);
}

// ── background thread ─────────────────────────────────────────────────────────

void ClangHighlighter::requestHighlight(EditorBuffer& buffer, BuildSystem* build)
{
    if (buffer.syntax_type != EditorBuffer::ST_C_CPP) return;
    if (buffer.is_new_file || buffer.filename.empty())  return;

    auto cache = buffer.semantic_cache;
    if (!cache) return;

    if (cache->in_progress.load()) {
        // A parse is already running; mark dirty so the caller knows
        // to re-request after the next save.
        cache->dirty.store(true);
        return;
    }

    cache->in_progress.store(true);
    cache->dirty.store(false);
    int my_version = ++cache->version;

    // ── Main-thread work: only the cheap stuff ────────────────────────────────
    // Snapshot buffer content while we're on the main thread (the user might
    // start editing before the thread finishes, and we want a consistent view).
    std::string content;
    content.reserve(static_cast<size_t>(buffer.total_lines) * 60);
    for (Line* l = buffer.document_head; l != nullptr; l = l->next)
        content += l->text + "\n";

    std::string abs_path = get_full_path(buffer.filename);
    // Snapshot the compiler settings (cheap copy of a plain struct).
    // getClangArguments() — which may run a Python subprocess — is deliberately
    // deferred to the background thread so the main thread returns immediately.
    CompilerSettings settings_snap = buffer.compiler_settings;

    std::thread([cache,
                 content      = std::move(content),
                 abs_path     = std::move(abs_path),
                 settings_snap,
                 my_version,
                 build]() mutable
    {
        // ── Thread: slow I/O happens here, not on the main thread ─────────────
        std::vector<std::string> args_str =
            build ? build->getClangArguments(abs_path, settings_snap)
                  : std::vector<std::string>{};

        std::vector<const char*> args;
        args.reserve(args_str.size());
        for (const auto& s : args_str) args.push_back(s.c_str());

        CXUnsavedFile uf;
        uf.Filename = abs_path.c_str();
        uf.Contents = content.c_str();
        uf.Length   = content.size();

        CXIndex index = clang_createIndex(0, 0);
        CXTranslationUnit tu = clang_parseTranslationUnit(
            index, abs_path.c_str(),
            args.data(), static_cast<int>(args.size()),
            &uf, 1,
            CXTranslationUnit_DetailedPreprocessingRecord |
            CXTranslationUnit_KeepGoing);

        auto result = std::make_shared<std::vector<std::vector<uint8_t>>>();

        if (tu) {
            CXFile file = clang_getFile(tu, abs_path.c_str());
            if (file) {
                CXSourceLocation start =
                    clang_getLocationForOffset(tu, file, 0);
                CXSourceLocation end =
                    clang_getLocationForOffset(tu, file,
                                               static_cast<unsigned>(content.size()));
                CXSourceRange range = clang_getRange(start, end);

                CXToken*  tokens     = nullptr;
                unsigned  num_tokens = 0;
                clang_tokenize(tu, range, &tokens, &num_tokens);

                if (tokens && num_tokens > 0) {
                    std::vector<CXCursor> cursors(num_tokens);
                    clang_annotateTokens(tu, tokens, num_tokens, cursors.data());

                    for (unsigned i = 0; i < num_tokens; ++i) {
                        // Bail out if a newer request has superseded us.
                        if (cache->version.load() != my_version) break;

                        CXString  spell = clang_getTokenSpelling(tu, tokens[i]);
                        std::string text = clang_getCString(spell);
                        clang_disposeString(spell);

                        int color = colorForToken(
                            clang_getTokenKind(tokens[i]), cursors[i], text);
                        if (color <= 0 || color > 255) continue;

                        CXSourceLocation loc =
                            clang_getTokenLocation(tu, tokens[i]);
                        unsigned line = 0, col = 0, offset = 0;
                        clang_getSpellingLocation(loc, nullptr, &line, &col, &offset);
                        if (line == 0 || col == 0) continue;

                        unsigned li = line - 1;
                        unsigned ci = col  - 1;

                        if (li >= result->size()) result->resize(li + 1);
                        auto& row = (*result)[li];
                        for (size_t k = 0; k < text.size(); ++k) {
                            if (ci + k >= row.size())
                                row.resize(ci + k + 1, 0);
                            row[ci + k] = static_cast<uint8_t>(color);
                        }
                    }
                    clang_disposeTokens(tu, tokens, num_tokens);
                }
            }

            // Walk the AST to build the definition index.  We do this while
            // the TU is still alive and only for non-system-header locations.
            auto def_map =
                std::make_shared<std::unordered_map<std::string, std::vector<SymbolDef>>>();

            clang_visitChildren(
                clang_getTranslationUnitCursor(tu),
                [](CXCursor c, CXCursor, CXClientData data) -> CXChildVisitResult {
                    if (!clang_isCursorDefinition(c))
                        return CXChildVisit_Recurse;

                    CXSourceLocation loc = clang_getCursorLocation(c);
                    if (clang_Location_isInSystemHeader(loc))
                        return CXChildVisit_Recurse;

                    CXFile   cx_file;
                    unsigned line, col, offset;
                    clang_getSpellingLocation(loc, &cx_file, &line, &col, &offset);
                    if (!cx_file || line == 0) return CXChildVisit_Recurse;

                    CXString cx_name = clang_getCursorSpelling(c);
                    std::string name = clang_getCString(cx_name);
                    clang_disposeString(cx_name);
                    if (name.empty()) return CXChildVisit_Recurse;

                    CXString cx_fname = clang_getFileName(cx_file);
                    std::string fname = clang_getCString(cx_fname);
                    clang_disposeString(cx_fname);

                    auto* map = static_cast<
                        std::unordered_map<std::string, std::vector<SymbolDef>>*>(data);
                    (*map)[name].push_back({std::move(fname), line, col});
                    return CXChildVisit_Recurse;
                },
                def_map.get());

            clang_disposeTranslationUnit(tu);

            // Commit colors + definition map when still the active version.
            if (cache->version.load() == my_version) {
                std::lock_guard<std::mutex> lk(cache->mutex);
                cache->colors = std::move(result);
                cache->definition_map.clear();
                for (auto& [name, defs] : *def_map)
                    cache->definition_map[name] = std::move(defs);
            }
        }
        clang_disposeIndex(index);

        cache->in_progress.store(false);
    }).detach();
}
