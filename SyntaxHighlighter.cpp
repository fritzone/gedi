#include "SyntaxHighlighter.h"
#include "utils.h"
#include <algorithm>
#include <cctype>

void SyntaxHighlighter::setSyntaxType(EditorBuffer& buffer) {
    buffer.syntax_type = EditorBuffer::ST_NONE;
    std::string lower_filename = buffer.filename;
    std::transform(lower_filename.begin(), lower_filename.end(), lower_filename.begin(), ::tolower);

    if (ends_with(lower_filename, ".c") || ends_with(lower_filename, ".h")) { buffer.syntax_type = EditorBuffer::ST_C_CPP; }
    else if (ends_with(lower_filename, ".cpp") || ends_with(lower_filename, ".hpp") || ends_with(lower_filename, ".cxx")) { buffer.syntax_type = EditorBuffer::ST_C_CPP; }
    else if (lower_filename == "makefile" || lower_filename == "gnumakefile") { buffer.syntax_type = EditorBuffer::ST_MAKEFILE; }
    else if (lower_filename == "cmakelists.txt") { buffer.syntax_type = EditorBuffer::ST_CMAKE; }
    else if (ends_with(lower_filename, ".s") || ends_with(lower_filename, ".asm")) { buffer.syntax_type = EditorBuffer::ST_ASSEMBLY; }
    else if (ends_with(lower_filename, ".ld")) { buffer.syntax_type = EditorBuffer::ST_LD_SCRIPT; }
    else if (ends_with(lower_filename, ".prim")) { buffer.syntax_type = EditorBuffer::PRIMAL; }
    else if (ends_with(lower_filename, ".glsl") || ends_with(lower_filename, ".vert") || ends_with(lower_filename, ".frag")) { buffer.syntax_type = EditorBuffer::ST_GLSL; }
    loadKeywords(buffer);
}

void SyntaxHighlighter::loadKeywords(EditorBuffer& buffer) {
    buffer.keywords.clear();
    if (buffer.syntax_type == EditorBuffer::ST_C_CPP || buffer.syntax_type == EditorBuffer::ST_GLSL) {
        const std::vector<std::string> keywords = { "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern", "float", "for", "goto", "if", "int", "long", "register", "return", "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "class", "public", "private", "protected", "new", "delete", "this", "friend", "virtual", "inline", "try", "catch", "throw", "namespace", "using", "template", "typename", "true", "false", "bool", "asm", "explicit", "operator", "nullptr" };
        for (const auto& kw : keywords) buffer.keywords[kw] = Renderer::CP_SYNTAX_KEYWORD;
    }
    if (buffer.syntax_type == EditorBuffer::ST_GLSL) {
        const std::vector<std::string> glsl_keywords = { "in", "out", "inout", "uniform", "layout", "centroid", "smooth", "flat", "noperspective", "attribute", "varying", "buffer", "shared", "coherent", "volatile", "restrict", "readonly", "writeonly", "resource", "atomic_uint", "group", "local_size_x", "local_size_y", "local_size_z", "std140", "std430", "packed", "binding", "location", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "bvec2", "bvec3", "bvec4", "uvec2", "uvec3", "uvec4", "dvec2", "dvec3", "dvec4", "mat2", "mat3", "mat4", "dmat2", "dmat3", "dmat4", "sampler1D", "sampler2D", "sampler3D", "samplerCube", "sampler2DRect", "sampler1DShadow", "sampler2DShadow", "samplerCubeShadow", "sampler2DRectShadow", "sampler1DArray", "sampler2DArray", "sampler1DArrayShadow", "sampler2DArrayShadow", "isampler1D", "isampler2D", "isampler3D", "isamplerCube", "isampler2DRect", "isampler1DArray", "isampler2DArray", "usampler1D", "usampler2D", "usampler3D", "usamplerCube", "usampler2DRect", "usampler1DArray", "usampler2DArray", "samplerBuffer", "isamplerBuffer", "usamplerBuffer", "sampler2DMS", "isampler2DMS", "usampler2DMS", "sampler2DMSArray", "isampler2DMSArray", "usampler2DMSArray", "image1D", "iimage1D", "uimage1D", "image2D", "iimage2D", "uimage2D", "image3D", "iimage3D", "uimage3D", "image2DRect", "iimage2DRect", "uimage2DRect", "imageCube", "iimageCube", "uimageCube", "imageBuffer", "iimageBuffer", "uimageBuffer", "image1DArray", "iimage1DArray", "uimage1DArray", "image2DArray", "iimage2DArray", "uimage2DArray", "image2DMS", "iimage2DMS", "uimage2DMS", "image2DMSArray", "iimage2DMSArray", "uimage2DMSArray", "discard", "precision", "highp", "mediump", "lowp" };
        for (const auto& kw : glsl_keywords) buffer.keywords[kw] = Renderer::CP_SYNTAX_KEYWORD;
    }
    else if (buffer.syntax_type == EditorBuffer::ST_CMAKE) {
        const std::vector<std::string> cmake_keywords = { "add_compile_definitions", "add_compile_options", "add_custom_command", "add_custom_target", "add_dependencies", "add_executable", "add_library", "add_link_options", "add_subdirectory", "add_test", "aux_source_directory", "break", "build_command", "cmake_minimum_required", "cmake_policy", "configure_file", "create_test_sourcelist", "define_property", "else", "elseif", "enable_language", "enable_testing", "endforeach", "endfunction", "endif", "endmacro", "endwhile", "execute_process", "export", "file", "find_file", "find_library", "find_package", "find_path", "find_program", "fltk_wrap_ui", "foreach", "function", "get_cmake_property", "get_directory_property", "get_filename_component", "get_property", "get_source_file_property", "get_target_property", "get_test_property", "if", "include", "include_directories", "include_external_msproject", "include_regular_expression", "install", "link_directories", "link_libraries", "list", "load_cache", "load_command", "macro", "mark_as_advanced", "math", "message", "option", "project", "qt_wrap_cpp", "qt_wrap_ui", "remove_definitions", "return", "separate_arguments", "set", "set_directory_properties", "set_property", "set_source_files_properties", "set_target_properties", "set_tests_properties", "site_name", "source_group", "string", "target_compile_definitions", "target_compile_features", "target_compile_options", "target_include_directories", "target_link_libraries", "target_link_options", "try_compile", "try_run", "unset", "variable_watch", "while" };
        for (const auto& kw : cmake_keywords) {
            std::string lower_kw = kw;
            std::transform(lower_kw.begin(), lower_kw.end(), lower_kw.begin(), ::tolower);
            buffer.keywords[lower_kw] = Renderer::CP_SYNTAX_KEYWORD;
        }
    } else if (buffer.syntax_type == EditorBuffer::ST_ASSEMBLY) {
        const std::vector<std::string> instructions = {"mov", "lea", "add", "sub", "mul", "imul", "div", "idiv", "inc", "dec", "and", "or", "xor", "not", "shl", "shr", "sal", "sar", "rol", "ror", "jmp", "je", "jne", "jz", "jnz", "jg", "jge", "jl", "jle", "ja", "jae", "jb", "jbe", "jc", "jnc", "call", "ret", "push", "pop", "cmp", "test", "syscall"};
        const std::vector<std::string> registers = {"rax", "eax", "ax", "al", "ah", "rbx", "ebx", "bx", "bl", "bh", "rcx", "ecx", "cx", "cl", "ch", "rdx", "edx", "dx", "dl", "dh", "rsi", "esi", "si", "sil", "rdi", "edi", "di", "dil", "rbp", "ebp", "bp", "bpl", "rsp", "esp", "sp", "spl", "r8", "r8d", "r8w", "r8b", "r9", "r9d", "r9w", "r9b", "r10", "r10d", "r10w", "r10b", "r11", "r11d", "r11w", "r11b", "r12", "r12d", "r12w", "r12b", "r13", "r13d", "r13w", "r13b", "r14", "r14d", "r14w", "r14b", "r15", "r15d", "r15w", "r15b"};
        const std::vector<std::string> directives = {".align", ".ascii", ".asciz", ".byte", ".data", ".double", ".equ", ".extern", ".file", ".float", ".global", ".globl", ".int", ".long", ".quad", ".section", ".short", ".size", ".string", ".text", ".type", ".word", ".zero"};
        for (const auto& kw : instructions) buffer.keywords[kw] = Renderer::CP_SYNTAX_KEYWORD;
        for (const auto& kw : registers) buffer.keywords["%" + kw] = Renderer::CP_SYNTAX_REGISTER_VAR;
        for (const auto& kw : directives) buffer.keywords[kw] = Renderer::CP_SYNTAX_PREPROCESSOR;
    } else if (buffer.syntax_type == EditorBuffer::ST_MAKEFILE) {
        const std::vector<std::string> directives = {"if", "ifeq", "ifneq", "else", "endif", "include", "define", "endef", "override", "export", "undefine"};
        const std::vector<std::string> variables = {"CC", "CXX", "CPP", "LD", "AS", "AR", "CFLAGS", "CXXFLAGS", "LDFLAGS", "ASFLAGS", "ARFLAGS", "RM", "SHELL"};
        for (const auto& kw : directives) buffer.keywords[kw] = Renderer::CP_SYNTAX_PREPROCESSOR;
        for (const auto& kw : variables) buffer.keywords[kw] = Renderer::CP_SYNTAX_REGISTER_VAR;
    } else if (buffer.syntax_type == EditorBuffer::ST_LD_SCRIPT) {
        const std::vector<std::string> keywords = {"ENTRY", "MEMORY", "SECTIONS", "INCLUDE", "OUTPUT_FORMAT", "OUTPUT_ARCH", "ASSERT", "ORIGIN", "LENGTH", "FILL"};
        const std::vector<std::string> functions = {"ALIGN", "DEFINED", "LOADADDR", "SIZEOF", "ADDR", "MAX", "MIN"};
        for (const auto& kw : keywords) buffer.keywords[kw] = Renderer::CP_SYNTAX_PREPROCESSOR;
        for (const auto& kw : functions) buffer.keywords[kw] = Renderer::CP_SYNTAX_KEYWORD;
    } else if (buffer.syntax_type == EditorBuffer::PRIMAL) {
        const std::vector<std::string> keywords = {"for", "let", "asm", "fun", "end", "next", "return", "var", "while", "goto", "if", "then", "else"};
        const std::vector<std::string> registers = {"$r0", "$r1", "$r2", "$r3", "$r4", "$r5", "$r6", "$r7",
                                                    "$r8", "$r9", "$r10", "$r11", "$r12", "$r13", "$r14", "$r15",
                                                    "$r16", "$r17", "$r18", "$r19", "$r20", "$r21", "$r22", "$r23",
                                                    "$r24", "$r25", "$r26", "$r27", "$r28", "$r29", "$r30", "$r31",
                                                    "$r32", "$r33", "$r34", "$r35", "$r36", "$r37", "$r38", "$r39",
                                                    "$r40", "$r41", "$r42", "$r43", "$r44", "$r45", "$r46", "$r47",
                                                    "$r48", "$r49", "$r50", "$r51", "$r52", "$r53", "$r54", "$r55",
                                                    "$r56", "$r57", "$r58", "$r59", "$r60", "$r61", "$r62", "$r63",
                                                    "$r64", "$r65", "$r66", "$r67", "$r68", "$r69", "$r70", "$r71",
                                                    "$r72", "$r73", "$r74", "$r75", "$r76", "$r77", "$r78", "$r79",
                                                    "$r80", "$r81", "$r82", "$r83", "$r84", "$r85", "$r86", "$r87",
                                                    "$r88", "$r89", "$r90", "$r91", "$r92", "$r93", "$r94", "$r95",
                                                    "$r96", "$r97", "$r98", "$r99", "$r100", "$r101", "$r102", "$r103",
                                                    "$r104", "$r105", "$r106", "$r107", "$r108", "$r109", "$r110", "$r111",
                                                    "$r112", "$r113", "$r114", "$r115", "$r116", "$r117", "$r118", "$r119",
                                                    "$r120", "$r121", "$r122", "$r123", "$r124", "$r125", "$r126", "$r127",
                                                    "$r128", "$r129", "$r130", "$r131", "$r132", "$r133", "$r134", "$r135",
                                                    "$r136", "$r137", "$r138", "$r139", "$r140", "$r141", "$r142", "$r143",
                                                    "$r144", "$r145", "$r146", "$r147", "$r148", "$r149", "$r150", "$r151",
                                                    "$r152", "$r153", "$r154", "$r155", "$r156", "$r157", "$r158", "$r159",
                                                    "$r160", "$r161", "$r162", "$r163", "$r164", "$r165", "$r166", "$r167",
                                                    "$r168", "$r169", "$r170", "$r171", "$r172", "$r173", "$r174", "$r175",
                                                    "$r176", "$r177", "$r178", "$r179", "$r180", "$r181", "$r182", "$r183",
                                                    "$r184", "$r185", "$r186", "$r187", "$r188", "$r189", "$r190", "$r191",
                                                    "$r192", "$r193", "$r194", "$r195", "$r196", "$r197", "$r198", "$r199",
                                                    "$r200", "$r201", "$r202", "$r203", "$r204", "$r205", "$r206", "$r207",
                                                    "$r208", "$r209", "$r210", "$r211", "$r212", "$r213", "$r214", "$r215",
                                                    "$r216", "$r217", "$r218", "$r219", "$r220", "$r221", "$r222", "$r223",
                                                    "$r224", "$r225", "$r226", "$r227", "$r228", "$r229", "$r230", "$r231",
                                                    "$r232", "$r233", "$r234", "$r235", "$r236", "$r237", "$r238", "$r239",
                                                    "$r240", "$r241", "$r242", "$r243", "$r244", "$r245", "$r246", "$r247",
                                                    "$r248", "$r249", "$r250", "$r251", "$r252", "$r253", "$r254", "$r255"};
        const std::vector<std::string> asm_keywords = {"ADD", "AND", "CALL", "COPY", "DIV", "DJMP", "DJNT", "DJT", "EQ", "GT", "GTE", "INC", "INTR", "JMP", "JNT", "JT", "LT", "LTE", "MOD", "MOV", "MUL", "NEQ", "NOT", "OR", "POP", "PUSH", "RET", "SUB", "XOR"};

        for (const auto& kw : asm_keywords) buffer.keywords[kw] = Renderer::CP_SYNTAX_KEYWORD;
        for (const auto& kw : keywords) buffer.keywords[kw] = Renderer::CP_SYNTAX_PREPROCESSOR;
        for (const auto& kw : registers) buffer.keywords[kw] = Renderer::CP_SYNTAX_REGISTER_VAR;
    }
}

std::vector<SyntaxToken> SyntaxHighlighter::parseLine(EditorBuffer& buffer, const std::string& line, const Renderer& renderer) {
    std::vector<SyntaxToken> tokens;
    if (line.empty()) {
        return tokens;
    }

    size_t i = 0;

    // If the previous line started a multiline comment, handle that first.
    if (buffer.in_multiline_comment) {
        size_t end_comment = line.find("*/");
        if (end_comment != std::string::npos) {
            tokens.push_back({line.substr(0, end_comment + 2), Renderer::CP_SYNTAX_COMMENT});
            buffer.in_multiline_comment = false;
            i = end_comment + 2;
        } else {
            tokens.push_back({line, Renderer::CP_SYNTAX_COMMENT});
            return tokens;
        }
    }

    // Check for preprocessor directives (lines starting with #)
    size_t first_char_pos = line.find_first_not_of(" \t");
    if (buffer.syntax_type != EditorBuffer::PRIMAL && first_char_pos != std::string::npos && line[first_char_pos] == '#') {
        i = first_char_pos;
        tokens.push_back({line.substr(0, i), Renderer::CP_DEFAULT_TEXT}); // Add leading whitespace

        size_t directive_end = i;
        while (directive_end < line.length() && !isspace(line[directive_end])) {
            directive_end++;
        }
        std::string directive = line.substr(i, directive_end - i);
        tokens.push_back({directive, Renderer::CP_SYNTAX_PREPROCESSOR});
        i = directive_end;

        // Special handling for <header.h> or "header.h" in #include
        if (directive == "#include") {
            size_t header_start = line.find_first_of("<\"", i);
            if (header_start != std::string::npos) {
                tokens.push_back({line.substr(i, header_start - i), Renderer::CP_DEFAULT_TEXT}); // Whitespace
                size_t header_end = line.find_first_of(">\"", header_start + 1);
                if (header_end != std::string::npos) {
                    tokens.push_back({line.substr(header_start, header_end - header_start + 1), Renderer::CP_SYNTAX_STRING});
                    i = header_end + 1;
                }
            }
        }

        // Add the rest of the line as default text
        if (i < line.length()) {
            tokens.push_back({line.substr(i), Renderer::CP_DEFAULT_TEXT});
        }
        return tokens;
    }

    // Main tokenizer loop
    while (i < line.length()) {
        // Check for single-line comments
        if (line.substr(i, 2) == "//") {
            tokens.push_back({line.substr(i), Renderer::CP_SYNTAX_COMMENT});
            break; // Rest of the line is a comment
        }

        if(buffer.syntax_type == EditorBuffer::PRIMAL) {
            if (line.substr(i, 1) == "#") {
                tokens.push_back({line.substr(i), Renderer::CP_SYNTAX_COMMENT});
                break; // Rest of the line is a comment
            }
        }

        // Check for multi-line comments
        if (line.substr(i, 2) == "/*") {
            size_t end_comment = line.find("*/", i + 2);
            if (end_comment != std::string::npos) {
                tokens.push_back({line.substr(i, end_comment + 2 - i), Renderer::CP_SYNTAX_COMMENT});
                i = end_comment + 2;
            } else {
                // Comment extends to the end of the line and beyond
                tokens.push_back({line.substr(i), Renderer::CP_SYNTAX_COMMENT});
                buffer.in_multiline_comment = true;
                break;
            }
            continue;
        }

        // Check for strings
        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i];
            size_t start = i;
            size_t end = start + 1;
            while (end < line.length() && (line[end] != quote || line[end - 1] == '\\')) {
                end++;
            }
            if (end < line.length()) end++;
            tokens.push_back({line.substr(start, end - start), Renderer::CP_SYNTAX_STRING});
            i = end;
            continue;
        }

        // Check for numbers (decimal, hex, binary)
        if (isdigit(line[i]) || (line[i] == '.' && i + 1 < line.length() && isdigit(line[i+1]))) {
            size_t start = i;
            if (i + 1 < line.length() && line[i] == '0' && (line[i+1] == 'x' || line[i+1] == 'X')) { // Hex
                i += 2;
                while (i < line.length() && isxdigit(line[i])) i++;
            } else if (i + 1 < line.length() && line[i] == '0' && (line[i+1] == 'b' || line[i+1] == 'B')) { // Binary
                i += 2;
                while (i < line.length() && (line[i] == '0' || line[i] == '1')) i++;
            } else { // Decimal or float
                while (i < line.length() && (isdigit(line[i]) || line[i] == '.')) i++;
            }
            // Check for number suffixes like f, u, l
            while (i < line.length() && (tolower(line[i]) == 'u' || tolower(line[i]) == 'l' || tolower(line[i]) == 'f')) i++;
            tokens.push_back({line.substr(start, i - start), Renderer::CP_SYNTAX_NUMBER});
            continue;
        }

        // Check for keywords or identifiers
        if (isalpha(line[i]) || line[i] == '_') {
            size_t start = i;
            while (i < line.length() && (isalnum(line[i]) || line[i] == '_')) i++;
            std::string word = line.substr(start, i - start);
            if (buffer.keywords.count(word)) {
                int color = buffer.keywords.at(word);
                int flags = renderer.getStyleFlags(static_cast<Renderer::ColorPairID>(color));
                tokens.push_back({word, color, flags});
            } else {
                tokens.push_back({word, Renderer::CP_DEFAULT_TEXT});
            }
            continue;
        }

        // Fallback for any other character (operators, punctuation, etc.)
        tokens.push_back({line.substr(i, 1), Renderer::CP_DEFAULT_TEXT});
        i++;
    }
    return tokens;
}
