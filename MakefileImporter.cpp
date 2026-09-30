#include "MakefileImporter.h"
#include "platform_compat.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

//  String helpers

bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

std::string ltrim(const std::string& s)
{
    size_t b = 0;
    while (b < s.size() && isBlank(s[b])) ++b;
    return s.substr(b);
}

std::string rtrim(const std::string& s)
{
    size_t e = s.size();
    while (e > 0 && isBlank(s[e - 1])) --e;
    return s.substr(0, e);
}

std::string trim(const std::string& s) { return rtrim(ltrim(s)); }

std::vector<std::string> words(const std::string& s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isBlank(s[i])) ++i;
        size_t b = i;
        while (i < s.size() && !isBlank(s[i])) ++i;
        if (i > b) out.push_back(s.substr(b, i - b));
    }
    return out;
}

std::string join(const std::vector<std::string>& v, const char* sep = " ")
{
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

bool endsWith(const std::string& s, const std::string& suf)
{
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to)
{
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// True if the line ends in an odd number of backslashes (a continuation).
bool continues(const std::string& s)
{
    size_t n = 0;
    for (size_t i = s.size(); i > 0 && s[i - 1] == '\\'; --i) ++n;
    return (n % 2) == 1;
}

// Does `t` start with directive keyword `kw` (followed by a separator)?
bool isDirective(const std::string& t, const char* kw)
{
    const size_t n = std::strlen(kw);
    if (t.compare(0, n, kw) != 0) return false;
    if (t.size() == n) return true;
    const char c = t[n];
    return isBlank(c) || c == '(' || c == '"' || c == '\'';
}

// Strip a make comment (first '#' not escaped as "\#"), then turn "\#" into "#".
std::string stripComment(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '#') { out += '#'; ++i; continue; }
        if (s[i] == '#') break;
        out += s[i];
    }
    return out;
}

// Index of the ')' / '}' that closes an opener at s[start-1].  Only the same
// bracket kind nests, like GNU make.
size_t findClose(const std::string& s, size_t start, char open, char close)
{
    int depth = 1;
    for (size_t i = start; i < s.size(); ++i) {
        if (s[i] == open) ++depth;
        else if (s[i] == close && --depth == 0) return i;
    }
    return std::string::npos;
}

// First occurrence of `ch` outside any $(...) / ${...} / (...) nesting.
size_t findTopLevel(const std::string& s, char ch, size_t from = 0)
{
    int depth = 0;
    for (size_t i = from; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '(' || c == '{') ++depth;
        else if ((c == ')' || c == '}') && depth > 0) --depth;
        else if (c == ch && depth == 0) return i;
    }
    return std::string::npos;
}

// Split function arguments on top-level commas; at most `max` pieces (-1 = no limit).
std::vector<std::string> splitArgs(const std::string& s, int max)
{
    std::vector<std::string> out;
    int depth = 0;
    size_t b = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '(' || c == '{') ++depth;
        else if ((c == ')' || c == '}') && depth > 0) --depth;
        else if (c == ',' && depth == 0 && (max < 0 || (int)out.size() < max - 1)) {
            out.push_back(s.substr(b, i - b));
            b = i + 1;
        }
    }
    out.push_back(s.substr(b));
    return out;
}

//  '%' patterns

bool patMatch(const std::string& pat, const std::string& word, std::string* stem = nullptr)
{
    const size_t p = pat.find('%');
    if (p == std::string::npos) {
        if (pat != word) return false;
        if (stem) stem->clear();
        return true;
    }
    const size_t suf_len = pat.size() - p - 1;
    if (word.size() < p + suf_len) return false;
    if (word.compare(0, p, pat, 0, p) != 0) return false;
    if (word.compare(word.size() - suf_len, suf_len, pat, p + 1, suf_len) != 0) return false;
    if (stem) *stem = word.substr(p, word.size() - p - suf_len);
    return true;
}

std::string patFill(const std::string& pat, const std::string& stem)
{
    const size_t p = pat.find('%');
    if (p == std::string::npos) return pat;
    return pat.substr(0, p) + stem + pat.substr(p + 1);
}

std::string patSubstWord(const std::string& pat, const std::string& rep, const std::string& w)
{
    std::string stem;
    if (!patMatch(pat, w, &stem)) return w;
    if (pat.find('%') == std::string::npos) return rep;
    return patFill(rep, stem);
}

//  Shell-like tokenizer (quotes, backslash escapes)

std::vector<std::string> shellSplit(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    bool have = false;
    char quote = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quote) {
            if (c == quote) quote = 0;
            else if (c == '\\' && quote == '"' && i + 1 < s.size()) cur += s[++i];
            else cur += c;
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; have = true; continue; }
        if (c == '\\' && i + 1 < s.size()) { cur += s[++i]; have = true; continue; }
        if (isBlank(c)) {
            if (have) { out.push_back(cur); cur.clear(); have = false; }
            continue;
        }
        cur += c;
        have = true;
    }
    if (have) out.push_back(cur);
    return out;
}

//  Glob matching (for $(wildcard))

bool globMatch(const char* p, const char* s)
{
    while (*p) {
        if (*p == '*') {
            while (*p == '*') ++p;
            if (!*p) return true;
            for (; *s; ++s)
                if (globMatch(p, s)) return true;
            return globMatch(p, s);
        }
        if (!*s) return false;
        if (*p == '?') { ++p; ++s; continue; }
        if (*p == '[') {
            const char* q = p + 1;
            bool neg = (*q == '!' || *q == '^');
            if (neg) ++q;
            bool hit = false;
            bool first = true;
            while (*q && (first || *q != ']')) {
                first = false;
                if (q[1] == '-' && q[2] && q[2] != ']') {
                    if (*s >= q[0] && *s <= q[2]) hit = true;
                    q += 3;
                } else {
                    if (*s == *q) hit = true;
                    ++q;
                }
            }
            if (*q != ']') {            // unterminated: treat '[' literally
                if (*s != '[') return false;
                ++p; ++s; continue;
            }
            if (hit == neg) return false;
            p = q + 1; ++s;
            continue;
        }
        if (*p == '\\' && p[1]) ++p;
        if (*p != *s) return false;
        ++p; ++s;
    }
    return *s == 0;
}

bool hasGlob(const std::string& s) { return s.find_first_of("*?[") != std::string::npos; }

//  File classification

std::string extOf(const std::string& p)
{
    const size_t sl = p.find_last_of("/\\");
    const size_t dot = p.rfind('.');
    if (dot == std::string::npos || (sl != std::string::npos && dot < sl)) return "";
    if (dot == 0 || (sl != std::string::npos && dot == sl + 1)) return "";   // dot-file
    return p.substr(dot);
}

bool isSourceFile(const std::string& p)
{
    static const std::unordered_set<std::string> exts = {
        ".c", ".cc", ".cpp", ".cxx", ".c++", ".C", ".cp", ".CPP", ".ixx", ".cppm",
        ".m", ".mm", ".s", ".S", ".sx", ".asm"
    };
    return exts.count(extOf(p)) != 0;
}

bool isHeaderFile(const std::string& p)
{
    static const std::unordered_set<std::string> exts = {
        ".h", ".hh", ".hpp", ".hxx", ".h++", ".H", ".inl", ".ipp", ".tpp", ".tcc", ".inc"
    };
    return exts.count(extOf(p)) != 0;
}

bool isObjectFile(const std::string& p)
{
    const std::string e = extOf(p);
    return e == ".o" || e == ".obj" || e == ".lo";
}

// "static_library", "shared_library" or "" from the file name alone.
std::string libraryKind(const std::string& p)
{
    const std::string e = extOf(p);
    if (e == ".a" || e == ".lib") return "static_library";
    if (e == ".so" || e == ".dylib" || e == ".dll") return "shared_library";
    const std::string fn = fs::path(p).filename().string();
    if (fn.find(".so.") != std::string::npos) return "shared_library";
    return "";
}

// Makefile fragments that are pure dependency output, not build logic.
bool isDependencyFile(const std::string& p)
{
    const std::string e = extOf(p);
    return e == ".d" || e == ".dep" || e == ".P" || e == ".Po" || e == ".Plo";
}

// Toolchain / OS locations we never want to pull into a project.
bool isSystemPath(const fs::path& p)
{
    const std::string s = p.generic_string();
    static const char* prefixes[] = {
        "/usr/", "/opt/", "/lib/", "/lib64/", "/Library/", "/System/", "/Applications/",
        "/nix/store/", "/gnu/store/", "/snap/"
    };
    for (const char* pre : prefixes)
        if (startsWith(s, pre)) return true;
    return s.find("/Program Files") != std::string::npos ||
           s.find("/Microsoft Visual Studio/") != std::string::npos ||
           s.find("/Windows Kits/") != std::string::npos ||
           s.find("/msys64/") != std::string::npos;
}

bool isRegularFile(const fs::path& p)
{
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

bool isDirectory(const fs::path& p)
{
    std::error_code ec;
    return fs::is_directory(p, ec);
}

fs::path absUnder(const fs::path& base, const std::string& p)
{
    fs::path q(p);
    fs::path r = (q.is_absolute() ? q : base / q).lexically_normal();
    if (!r.has_filename() && r.has_relative_path()) r = r.parent_path();   // "dir/" → "dir"
    return r;
}

// Does a shell command word name a compiler, linker or archiver?
bool isToolchainCommand(const std::string& tok)
{
    std::string b = fs::path(tok).filename().string();
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (endsWith(b, ".exe")) b.resize(b.size() - 4);
    // strip a version suffix: gcc-13, clang++-18, g++-12.2
    {
        size_t dash = b.rfind('-');
        if (dash != std::string::npos && dash + 1 < b.size() &&
            std::all_of(b.begin() + (long)dash + 1, b.end(),
                        [](char c) { return std::isdigit((unsigned char)c) || c == '.'; }))
            b.resize(dash);
    }
    // strip a cross prefix: arm-none-eabi-gcc, x86_64-w64-mingw32-g++
    static const char* tools[] = {
        "gcc", "g++", "cc", "c++", "clang", "clang++", "ld", "ar", "llvm-ar", "gcc-ar",
        "icc", "icpc", "icx", "icpx", "tcc", "cl", "link", "lib", "nvcc", "emcc", "em++",
        "gfortran", "zig", "ld.lld", "ld.gold", "ld.bfd", "lld", "lld-link", "libtool",
        "as", "nasm", "yasm",
        // Open Watcom and other DOS-era toolchains
        "wlink", "wlib", "wcl", "wcl386", "wcc", "wcc386", "wpp", "wpp386", "wasm", "owcc",
        "wfc", "wfc386", "jwasm", "ml", "ml64", "masm", "tasm", "tlink", "tlib", "bcc", "bcc32",
        "ilink32"
    };
    for (const char* t : tools) {
        const std::string ts(t);
        if (b == ts) return true;
        if (b.size() > ts.size() && endsWith(b, "-" + ts)) return true;
    }
    return false;
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

//  Case-insensitive file lookup
// DOS-era trees (Watcom projects) often spell names differently from the
// files on disk: CLOUD.CPP vs cloud.cpp.  Directory listings are cached per import.

std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& noCaseDirs()
{
    static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> dirs;
    return dirs;
}

fs::path findNoCase(const fs::path& p, int depth = 0)
{
    std::error_code ec;
    if (fs::exists(p, ec)) return p;
    if (depth > 64) return {};
    const fs::path parent = p.parent_path();
    const std::string name = p.filename().string();
    if (name.empty() || parent.empty() || parent == p) return {};
    const fs::path real_parent = findNoCase(parent, depth + 1);
    if (real_parent.empty()) return {};

    auto& cache = noCaseDirs();
    const std::string key = real_parent.generic_string();
    auto it = cache.find(key);
    if (it == cache.end()) {
        std::unordered_map<std::string, std::string> names;
        for (fs::directory_iterator d(real_parent, fs::directory_options::skip_permission_denied, ec), end;
             !ec && d != end; d.increment(ec)) {
            const std::string n = d->path().filename().string();
            names.emplace(lower(n), n);
        }
        it = cache.emplace(key, std::move(names)).first;
    }
    auto f = it->second.find(lower(name));
    if (f == it->second.end()) return {};
    return real_parent / f->second;
}

// DOS makefiles write paths with backslashes.
std::string slashes(std::string s)
{
#ifndef _WIN32
    std::replace(s.begin(), s.end(), '\\', '/');
#endif
    return s;
}

//  Open Watcom wmake dialect detection

bool sniffWatcom(const fs::path& file)
{
    const std::string e = lower(file.extension().string());
    if (e == ".mif" || e == ".wat" || e == ".wmk") return true;

    std::ifstream f(file, std::ios::binary);
    std::string line;
    int lines = 0;
    while (std::getline(f, line) && ++lines < 4000) {
        size_t i = 0;
        while (i < line.size() && isBlank(line[i])) ++i;
        if (i >= line.size()) continue;
        if (line[i] == '!') {
            size_t j = i + 1;
            while (j < line.size() && isBlank(line[j])) ++j;
            size_t k = j;
            while (k < line.size() && std::isalpha((unsigned char)line[k])) ++k;
            const std::string kw = lower(line.substr(j, k - j));
            static const char* kws[] = {"include", "ifdef", "ifndef", "ifeq", "ifneq", "ifeqi", "ifneqi",
                                        "if", "else", "endif", "define", "undef", "error", "message",
                                        "loaddll", "inject", "elseifdef", "elseifndef", "elseifeq",
                                        "elseifneq", "elseifeqi", "elseifneqi"};
            for (const char* w : kws)
                if (kw == w) return true;
        }
        const std::string low = lower(line);
        if (low.find(".symbolic") != std::string::npos || low.find(".extensions") != std::string::npos ||
            low.find("$[*") != std::string::npos || low.find("$[@") != std::string::npos ||
            low.find("$^@") != std::string::npos)
            return true;
    }
    return false;
}

//  wmake `!if` expressions

struct ExprVal {
    bool        num = true;
    long        n = 0;
    std::string s;
    bool truthy() const { return num ? n != 0 : !s.empty(); }
    std::string str() const { return num ? std::to_string(n) : s; }
};

class WmakeExpr {
public:
    WmakeExpr(const std::string& text,
              std::function<bool(const std::string&)> defined,
              std::function<bool(const std::string&)> exists)
        : m_s(text), m_defined(std::move(defined)), m_exists(std::move(exists)) {}

    bool eval() { return parseOr().truthy(); }

private:
    const std::string& m_s;
    size_t m_i = 0;
    std::function<bool(const std::string&)> m_defined, m_exists;

    static ExprVal num(long v) { ExprVal r; r.n = v; return r; }
    void ws() { while (m_i < m_s.size() && isBlank(m_s[m_i])) ++m_i; }
    bool peek(const char* op) { ws(); return m_s.compare(m_i, std::strlen(op), op) == 0; }
    bool eat(const char* op) { if (!peek(op)) return false; m_i += std::strlen(op); return true; }

    ExprVal parseOr()
    {
        ExprVal l = parseAnd();
        while (eat("||")) { ExprVal r = parseAnd(); l = num(l.truthy() || r.truthy()); }
        return l;
    }
    ExprVal parseAnd()
    {
        ExprVal l = parseCmp();
        while (eat("&&")) { ExprVal r = parseCmp(); l = num(l.truthy() && r.truthy()); }
        return l;
    }
    ExprVal parseCmp()
    {
        ExprVal l = parseAdd();
        for (;;) {
            static const char* ops[] = {"==", "!=", "<=", ">=", "<", ">"};
            const char* op = nullptr;
            for (const char* o : ops) if (peek(o)) { op = o; break; }
            if (!op) return l;
            m_i += std::strlen(op);
            ExprVal r = parseAdd();
            int c;
            if (l.num && r.num) c = (l.n < r.n) ? -1 : (l.n > r.n) ? 1 : 0;
            else c = l.str().compare(r.str());
            const std::string o(op);
            const bool res = o == "==" ? c == 0 : o == "!=" ? c != 0 : o == "<=" ? c <= 0 :
                             o == ">=" ? c >= 0 : o == "<"  ? c < 0  : c > 0;
            l = num(res);
        }
    }
    ExprVal parseAdd()
    {
        ExprVal l = parseMul();
        for (;;) {
            if (eat("+")) l = num(l.n + parseMul().n);
            else if (peek("-")) { ++m_i; l = num(l.n - parseMul().n); }
            else return l;
        }
    }
    ExprVal parseMul()
    {
        ExprVal l = parseUnary();
        for (;;) {
            if (eat("*")) l = num(l.n * parseUnary().n);
            else if (eat("/")) { const long r = parseUnary().n; l = num(r ? l.n / r : 0); }
            else if (eat("%")) { const long r = parseUnary().n; l = num(r ? l.n % r : 0); }
            else return l;
        }
    }
    ExprVal parseUnary()
    {
        if (peek("!") && !peek("!=")) { ++m_i; return num(!parseUnary().truthy()); }
        if (eat("~")) return num(~parseUnary().n);
        if (eat("-")) return num(-parseUnary().n);
        return primary();
    }
    ExprVal primary()
    {
        ws();
        if (m_i >= m_s.size()) return num(0);
        if (m_s[m_i] == '(') {
            ++m_i;
            ExprVal v = parseOr();
            eat(")");
            return v;
        }
        if (m_s[m_i] == '"') {
            const size_t e = m_s.find('"', m_i + 1);
            ExprVal v;
            v.num = false;
            v.s = m_s.substr(m_i + 1, e == std::string::npos ? std::string::npos : e - m_i - 1);
            m_i = (e == std::string::npos) ? m_s.size() : e + 1;
            return v;
        }
        const size_t b = m_i;
        while (m_i < m_s.size() && !isBlank(m_s[m_i]) && std::strchr("()!=<>&|+*%~\"", m_s[m_i]) == nullptr)
            ++m_i;
        const std::string word = m_s.substr(b, m_i - b);
        const std::string lw = lower(word);
        if ((lw == "defined" || lw == "exist" || lw == "exists") && peek("(")) {
            ++m_i;
            const size_t e = m_s.find(')', m_i);
            const std::string arg = trim(m_s.substr(m_i, e == std::string::npos ? std::string::npos : e - m_i));
            m_i = (e == std::string::npos) ? m_s.size() : e + 1;
            return num(lw == "defined" ? m_defined(arg) : m_exists(arg));
        }
        if (word.empty()) { ++m_i; return num(0); }
        char* end = nullptr;
        const long v = std::strtol(word.c_str(), &end, 0);
        if (end && *end == 0) return num(v);
        ExprVal r;
        r.num = false;
        r.s = word;
        return r;
    }
};

//  Shared import state

struct SharedState {
    MakefileImporter::Options opts;
    std::vector<std::string> warnings;
    std::vector<fs::path>    makefiles;
    std::set<std::string>    makefile_set;
    std::map<std::string, std::string> shell_cache;

    void warn(const std::string& w)
    {
        if (warnings.size() < 100 &&
            std::find(warnings.begin(), warnings.end(), w) == warnings.end())
            warnings.push_back(w);
    }

    void addMakefile(const fs::path& p)
    {
        if (makefile_set.insert(p.generic_string()).second) makefiles.push_back(p);
    }
};

std::string runShell(SharedState& st, const std::string& cmd, const fs::path& cwd)
{
    const std::string key = cwd.string() + '\x01' + cmd;
    auto it = st.shell_cache.find(key);
    if (it != st.shell_cache.end()) return it->second;

#ifdef _WIN32
    const std::string full = "cd /d \"" + cwd.string() + "\" && " + cmd + " 2>NUL";
#else
    std::string quoted = cwd.string();
    quoted = "'" + replaceAll(quoted, "'", "'\\''") + "'";
    const std::string full = "cd " + quoted + " && ( " + cmd + " ) 2>/dev/null </dev/null";
#endif
    std::string out;
    if (FILE* p = popen(full.c_str(), "r")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), p)) > 0 && out.size() < (1u << 20))
            out.append(buf, n);
        pclose(p);
    }
    // make: trailing newlines dropped, the rest become spaces
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    for (char& c : out)
        if (c == '\n' || c == '\r') c = ' ';
    st.shell_cache.emplace(key, out);
    return out;
}

//  Parsed makefile model

struct Var {
    std::string value;
    bool        recursive = true;
    std::string origin    = "file";   // default | environment | file | command line | override
    bool        exported  = false;
};

struct Rule {
    std::vector<std::string> targets;
    std::vector<std::string> prereqs;
    std::vector<std::string> order_only;
    std::vector<std::string> recipe;   // unexpanded command lines
    std::string stem;                  // static pattern rules only
    bool pattern = false;
};

struct SubMake {
    fs::path dir;
    std::string file;                  // empty: default makefile lookup
    std::vector<std::pair<std::string, std::string>> vars;
    bool watcom = false;               // invoked as wmake
};

struct FoundTarget {
    fs::path    output;                // absolute
    std::string type;                  // executable / static_library / shared_library
    std::vector<fs::path> files;       // sources then headers, unique
    std::set<std::string> seen;
    std::vector<std::string> links;    // absolute lib paths or "-lname"

    void add(const fs::path& p)
    {
        if (seen.insert(p.generic_string()).second) files.push_back(p);
    }
};

struct ContextResult {
    fs::path cwd;
    std::vector<FoundTarget> targets;
    std::vector<fs::path>    loose;    // project files referenced but not owned by a target
    std::vector<fs::path>    include_dirs;
    std::vector<std::string> defines;
    std::string              cpp_std;
    bool                     watcom = false;
};

std::string wmakeFileMacro(const std::string& f, char mod);
std::string dosSlashes(std::string s);

// ═══════════════════════════════════════════════════════════════════════════════
// MakeReader - one make invocation: a working directory, its variables and rules
// ═══════════════════════════════════════════════════════════════════════════════
class MakeReader {
public:
    MakeReader(SharedState& st, const fs::path& cwd) : m_st(st), m_cwd(cwd) { initDefaults(); }

    void setCommandLineVar(const std::string& name, const std::string& value)
    {
        Var v;
        v.value = value;
        v.recursive = true;
        v.origin = "command line";
        v.exported = true;
        m_vars[name] = v;
        m_cmdline.emplace_back(name, value);
    }

    void setEnvironmentVar(const std::string& name, const std::string& value)
    {
        auto it = m_vars.find(name);
        if (it != m_vars.end() && it->second.origin == "command line") return;
        Var v;
        v.value = value;
        v.recursive = false;
        v.origin = "environment";
        v.exported = true;
        m_vars[name] = v;
    }

    bool readMakefile(const fs::path& path, bool must_exist)
    {
        const fs::path abs = path.lexically_normal();
        if (!isRegularFile(abs)) {
            if (must_exist) m_st.warn("Makefile not found: " + abs.string());
            return false;
        }
        if (m_include_depth > 32) {
            m_st.warn("Include nesting too deep at " + abs.string());
            return false;
        }
        std::ifstream f(abs, std::ios::binary);
        if (!f) {
            if (must_exist) m_st.warn("Cannot read " + abs.string());
            return false;
        }
        m_st.addMakefile(abs);
        {
            Var& ml = m_vars["MAKEFILE_LIST"];
            ml.recursive = false;
            ml.value += (ml.value.empty() ? "" : " ") + abs.generic_string();
        }

        std::vector<std::string> lines;
        std::string ln;
        while (std::getline(f, ln)) {
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            lines.push_back(std::move(ln));
        }

        ++m_include_depth;
        const std::string saved = m_cur_file;
        m_cur_file = abs.string();
        parseLines(lines);
        m_cur_file = saved;
        --m_include_depth;
        return true;
    }

    std::vector<SubMake> findSubMakes();
    ContextResult analyze();

    const fs::path& cwd() const { return m_cwd; }

    // Switch to Open Watcom wmake syntax (before reading anything).
    void setWatcom(bool on)
    {
        m_watcom = on;
        if (!on) return;
        // GNU make's built-in variables don't exist in wmake (they'd satisfy !ifdef)
        static const std::set<std::string> keep = {"CURDIR", "MAKEFLAGS", "MAKECMDGOALS", "SHELL"};
        for (auto it = m_vars.begin(); it != m_vars.end();)
            it = (it->second.origin == "default" && !keep.count(it->first)) ? m_vars.erase(it) : std::next(it);
        Var v;
        v.recursive = false;
        v.origin = "default";
        v.value = "wmake";
        m_vars["MAKE"] = v;
        v.value = "";
#ifdef _WIN32
        m_vars["__NT__"] = v;
#elif defined(__APPLE__)
        m_vars["__UNIX__"] = v;
#else
        m_vars["__LINUX__"] = v;
        m_vars["__UNIX__"] = v;
#endif
    }
    bool watcom() const { return m_watcom; }

    // Directories named by `.h : dir;dir` search paths (for header lookup).
    std::vector<fs::path> headerSearchDirs() const
    {
        std::vector<fs::path> out;
        for (const auto& [ext, dirs] : m_ext_paths)
            if (isHeaderFile("x" + ext))
                for (const auto& d : dirs) out.push_back(absUnder(m_cwd, d));
        return out;
    }
    const std::vector<std::pair<std::string, std::string>>& commandLineVars() const { return m_cmdline; }

    std::vector<std::pair<std::string, std::string>> exportedVars()
    {
        std::vector<std::pair<std::string, std::string>> out;
        for (auto& [name, v] : m_vars) {
            if (v.origin == "default" || v.origin == "command line") continue;
            if (name == "MAKEFILE_LIST" || name == "CURDIR" || name == ".DEFAULT_GOAL") continue;
            if (v.exported || (m_export_all && v.origin != "environment")) {
                m_analysis = true;
                out.emplace_back(name, v.recursive ? expand(v.value) : v.value);
                m_analysis = false;
            }
        }
        return out;
    }

    fs::path top_makefile;

private:
    SharedState& m_st;
    fs::path     m_cwd;

    std::unordered_map<std::string, Var> m_vars;
    std::vector<Rule> m_rules;
    std::vector<std::pair<std::string, std::vector<std::string>>> m_vpaths;
    std::set<std::string> m_phony;
    std::string m_default_goal;
    std::vector<std::pair<std::string, std::string>> m_cmdline;
    bool m_export_all = false;
    bool m_watcom = false;                                          // Open Watcom wmake dialect
    std::map<std::string, std::vector<std::string>> m_ext_paths;   // wmake `.cpp : dir;dir`

    std::vector<std::unordered_map<std::string, std::string>> m_scopes;   // $(call)/$(foreach)/automatic
    std::unordered_set<std::string> m_expanding;
    int  m_expand_depth  = 0;
    int  m_call_depth    = 0;
    int  m_include_depth = 0;
    int  m_eval_depth    = 0;
    bool m_no_shell      = false;   // while expanding recipes
    bool m_analysis      = false;   // $(eval)/$(error) inert while inspecting
    char m_recipe_prefix = '\t';
    std::string m_cur_file;

    // analysis caches
    std::unordered_map<std::string, std::vector<int>> m_explicit;
    std::vector<int> m_pattern_rules;
    std::unordered_map<std::string, fs::path> m_resolve_cache;
    std::unordered_map<std::string, std::vector<fs::path>> m_stem_index;
    bool m_stem_index_built = false;

    void initDefaults();
    void parseLines(const std::vector<std::string>& lines);
    void processLine(const std::string& line, std::vector<int>& out_rules, int reparse = 0);
    void parseRule(const std::string& t, size_t colon, std::vector<int>& out_rules);
    bool evalCondition(const std::string& kw, const std::string& args);
    bool evalWatcomCondition(const std::string& kw, const std::string& args);

    // Line continuation: backslash for GNU make, '&' for wmake.
    bool lineContinues(const std::string& s) const
    {
        if (!m_watcom) return continues(s);
        const std::string r = rtrim(s);
        return !r.empty() && r.back() == '&';
    }
    std::string dropContinuation(const std::string& s) const
    {
        if (!m_watcom) return s.substr(0, s.size() - 1);
        std::string r = rtrim(s);
        r.pop_back();
        return r;
    }
    void assign(const std::string& name, const std::string& op, const std::string& value,
                bool is_override, bool is_export);

    std::string expand(const std::string& s);
    std::string expandRef(const std::string& inner);
    std::string lookup(const std::string& name);
    std::string callFunction(const std::string& name, const std::vector<std::string>& args);
    std::vector<std::string> wildcard(const std::string& pattern);
    std::string shell(const std::string& cmd);

    bool isDefined(const std::string& name) const
    {
        return m_vars.count(name) || std::getenv(name.c_str()) != nullptr;
    }

    // analysis
    void buildIndexes();
    fs::path resolve(const std::string& name);
    std::vector<fs::path> objectInputs(const std::string& obj);
    void buildStemIndex();
    std::string expandRecipe(const std::vector<std::string>& recipe, const std::string& target,
                             const std::vector<std::string>& prereqs,
                             const std::vector<std::string>& order_only,
                             const std::string& stem);
    void scanShell(const std::string& cmd, const fs::path& dir, std::vector<SubMake>& out, int depth);
    void scanFlags(const std::string& text, ContextResult& cr);
};

void MakeReader::initDefaults()
{
    auto def = [&](const char* name, const char* value, bool recursive = true) {
        Var v;
        if (const char* e = std::getenv(name)) {
            v.value = e;
            v.recursive = false;
            v.origin = "environment";
        } else {
            v.value = value;
            v.recursive = recursive;
            v.origin = "default";
        }
        m_vars[name] = v;
    };
    def("MAKE", "make");
    def("CC", "cc");
    def("CXX", "g++");
    def("CPP", "$(CC) -E");
    def("AR", "ar");
    def("ARFLAGS", "rv");
    def("AS", "as");
    def("LD", "ld");
    def("RM", "rm -f");
    def("LEX", "lex");
    def("YACC", "yacc");
    def("COMPILE.c", "$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c");
    def("COMPILE.cc", "$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c");
    def("COMPILE.cpp", "$(COMPILE.cc)");
    def("LINK.o", "$(CC) $(LDFLAGS) $(TARGET_ARCH)");
    def("LINK.c", "$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH)");
    def("LINK.cc", "$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH)");
    def("LINK.cpp", "$(LINK.cc)");
    def("OUTPUT_OPTION", "-o $@");
    def("MAKE_VERSION", "4.3");

    Var v;
    v.recursive = false;
    v.origin = "default";
#ifdef _WIN32
    v.value = "cmd.exe";
#else
    v.value = "/bin/sh";
#endif
    m_vars["SHELL"] = v;
    v.value = m_cwd.generic_string();
    m_vars["CURDIR"] = v;
    v.value = "";
    m_vars["MAKEFLAGS"] = v;
    m_vars["MAKECMDGOALS"] = v;
    v.value = ".c .cc .cpp .C .o .s .S .h";
    m_vars["SUFFIXES"] = v;
}

//  Line reader

void MakeReader::parseLines(const std::vector<std::string>& L)
{
    struct Cond { bool parent_active; bool taken; bool active; };
    std::vector<Cond> conds;
    auto active = [&] { return conds.empty() || conds.back().active; };

    std::vector<int> cur_rules;

    bool        in_define = false;
    int         define_depth = 0;
    bool        define_skip = false, define_override = false, define_export = false;
    std::string define_name, define_op, define_body;
    bool        define_first = true;

    for (size_t i = 0; i < L.size(); ++i) {
        const std::string& raw = L[i];

        //  define ... endef body
        if (in_define) {
            const std::string t = trim(raw);
            if (isDirective(t, "endef")) {
                if (--define_depth == 0) {
                    in_define = false;
                    if (!define_skip)
                        assign(define_name, define_op, define_body, define_override, define_export);
                    continue;
                }
            } else if (isDirective(t, "define")) {
                ++define_depth;
            }
            if (!define_first) define_body += '\n';
            define_body += raw;
            define_first = false;
            continue;
        }

        //  Recipe line (wmake: any indentation; GNU make: the recipe prefix)
        const bool indented = !raw.empty() &&
            (raw[0] == m_recipe_prefix || (m_watcom && isBlank(raw[0]) && !trim(raw).empty()));
        if (!cur_rules.empty() && indented) {
            std::string cmd = m_watcom ? ltrim(raw) : raw.substr(1);
            while (lineContinues(cmd) && i + 1 < L.size()) {
                cmd = dropContinuation(cmd);
                std::string nx = L[++i];
                if (!nx.empty() && nx[0] == m_recipe_prefix) nx.erase(0, 1);
                cmd += " " + ltrim(nx);
            }
            if (active())
                for (int r : cur_rules) m_rules[(size_t)r].recipe.push_back(cmd);
            continue;
        }

        //  Logical line: join continuations, drop comments
        std::string line = raw;
        while (lineContinues(line) && i + 1 < L.size())
            line = rtrim(dropContinuation(line)) + " " + ltrim(L[++i]);
        const std::string t = trim(stripComment(line));
        if (t.empty()) continue;

        //  wmake preprocessor:  !include !ifdef !ifeq !if !else !endif !define ...
        if (m_watcom && t[0] == '!') {
            const std::string d = ltrim(t.substr(1));
            size_t k = 0;
            while (k < d.size() && std::isalpha((unsigned char)d[k])) ++k;
            const std::string kw = lower(d.substr(0, k));
            const std::string rest = trim(d.substr(k));
            auto isIf = [](const std::string& w) {
                return w == "ifdef" || w == "ifndef" || w == "ifeq" || w == "ifneq" ||
                       w == "ifeqi" || w == "ifneqi" || w == "if";
            };

            if (isIf(kw)) {
                const bool parent = active();
                const bool r = parent && evalWatcomCondition(kw, rest);
                conds.push_back({parent, r, r});
                continue;
            }
            if (kw == "else" || kw.rfind("elseif", 0) == 0) {
                std::string ckw, crest;
                if (kw == "else") {                          // "!else" or "!else ifdef X"
                    size_t k2 = 0;
                    while (k2 < rest.size() && std::isalpha((unsigned char)rest[k2])) ++k2;
                    const std::string w = lower(rest.substr(0, k2));
                    if (isIf(w)) { ckw = w; crest = trim(rest.substr(k2)); }
                } else {                                     // "!elseifdef X" → "ifdef"
                    ckw = kw.substr(4);
                    crest = rest;
                }
                if (conds.empty()) { m_st.warn("'!else' without '!if' in " + m_cur_file); continue; }
                Cond& c = conds.back();
                if (ckw.empty()) {
                    c.active = c.parent_active && !c.taken;
                    c.taken = true;
                } else if (!c.parent_active || c.taken) {
                    c.active = false;
                } else {
                    const bool r = evalWatcomCondition(ckw, crest);
                    c.active = r;
                    c.taken = r;
                }
                continue;
            }
            if (kw == "endif") {
                if (conds.empty()) m_st.warn("'!endif' without '!if' in " + m_cur_file);
                else conds.pop_back();
                continue;
            }
            if (!active()) continue;

            cur_rules.clear();
            if (kw == "include") {
                std::string f = trim(expand(rest));
                if (f.size() >= 2 && ((f.front() == '"' && f.back() == '"') || (f.front() == '<' && f.back() == '>')))
                    f = f.substr(1, f.size() - 2);
                fs::path p = absUnder(m_cwd, slashes(f));
                if (!isRegularFile(p))
                    if (fs::path q = findNoCase(p); !q.empty()) p = q;
                readMakefile(p, true);
            } else if (kw == "define") {
                const auto w = words(rest);
                if (!w.empty()) assign(w[0], "=", trim(rest.substr(rest.find(w[0]) + w[0].size())), false, false);
            } else if (kw == "undef") {
                m_vars.erase(trim(expand(rest)));
            } else if (kw == "error") {
                m_st.warn("!error " + expand(rest) + " in " + m_cur_file + " (ignored)");
            }
            // !message, !loaddll, !inject: nothing to do
            continue;
        }

        //  Conditionals (always tracked, even inside a false branch)
        if (isDirective(t, "ifeq") || isDirective(t, "ifneq") ||
            isDirective(t, "ifdef") || isDirective(t, "ifndef")) {
            const size_t kwlen = t.find_first_of(" \t(\"'");
            const std::string kw = t.substr(0, kwlen);
            const bool parent = active();
            const bool r = parent && evalCondition(kw, trim(t.substr(kw.size())));
            conds.push_back({parent, r, r});
            continue;
        }
        if (isDirective(t, "else")) {
            if (conds.empty()) { m_st.warn("'else' without 'if' in " + m_cur_file); continue; }
            Cond& c = conds.back();
            const std::string rest = trim(t.substr(4));
            if (rest.empty()) {
                c.active = c.parent_active && !c.taken;
                c.taken = true;
            } else {
                const size_t kwlen = rest.find_first_of(" \t(\"'");
                const std::string kw = rest.substr(0, kwlen);
                if (!c.parent_active || c.taken) {
                    c.active = false;
                } else {
                    const bool r = evalCondition(kw, trim(rest.substr(kw.size())));
                    c.active = r;
                    c.taken = r;
                }
            }
            continue;
        }
        if (isDirective(t, "endif")) {
            if (conds.empty()) m_st.warn("'endif' without 'if' in " + m_cur_file);
            else conds.pop_back();
            continue;
        }

        //  define (the body must be skipped correctly even when inactive)
        {
            std::string d = t;
            bool ov = false, ex = false;
            for (;;) {
                if (isDirective(d, "override")) { ov = true; d = ltrim(d.substr(8)); continue; }
                if (isDirective(d, "export"))   { ex = true; d = ltrim(d.substr(6)); continue; }
                if (isDirective(d, "private"))  { d = ltrim(d.substr(7)); continue; }
                break;
            }
            if (isDirective(d, "define")) {
                std::string rest = trim(d.substr(6));
                std::string op = "=";
                for (const char* o : {":::=", "::=", ":=", "+=", "?=", "!=", "="}) {
                    if (endsWith(rest, o)) {
                        op = o;
                        rest = trim(rest.substr(0, rest.size() - std::strlen(o)));
                        break;
                    }
                }
                in_define       = true;
                define_depth    = 1;
                define_skip     = !active();
                define_override = ov;
                define_export   = ex;
                define_name     = define_skip ? rest : trim(expand(rest));
                define_op       = op;
                define_body.clear();
                define_first    = true;
                cur_rules.clear();
                continue;
            }
        }

        if (!active()) continue;

        std::vector<int> new_rules;
        processLine(t, new_rules);
        cur_rules = std::move(new_rules);
    }

    if (in_define) m_st.warn("Unterminated 'define' in " + m_cur_file);
    if (!conds.empty()) m_st.warn("Unterminated conditional in " + m_cur_file);
}

bool MakeReader::evalWatcomCondition(const std::string& kw, const std::string& args)
{
    if (kw == "ifdef" || kw == "ifndef") {
        const std::string name = trim(expand(args));
        const bool defined = isDefined(name);
        return (kw == "ifdef") ? defined : !defined;
    }
    if (kw == "if") {
        const std::string e = expand(args);
        WmakeExpr x(e,
                    [&](const std::string& n) { return isDefined(n); },
                    [&](const std::string& f) {
                        const fs::path p = absUnder(m_cwd, slashes(f));
                        std::error_code ec;
                        return fs::exists(p, ec) || !findNoCase(p).empty();
                    });
        return x.eval();
    }
    // !ifeq MACRO value / !ifneq / case-insensitive !ifeqi / !ifneqi
    const auto w = words(args);
    if (w.empty()) return kw.rfind("ifneq", 0) == 0;
    const std::string mv = trim(lookup(expand(w[0])));
    const std::string val = trim(expand(args.substr(args.find(w[0]) + w[0].size())));
    const bool ci = kw.back() == 'i';
    const bool eq = ci ? lower(mv) == lower(val) : mv == val;
    return (kw.rfind("ifeq", 0) == 0) ? eq : !eq;
}

bool MakeReader::evalCondition(const std::string& kw, const std::string& args)
{
    if (kw == "ifdef" || kw == "ifndef") {
        const std::string name = trim(expand(args));
        auto it = m_vars.find(name);
        bool defined = false;
        if (it != m_vars.end()) defined = !it->second.value.empty();
        else if (const char* e = std::getenv(name.c_str())) defined = *e != 0;
        return (kw == "ifdef") ? defined : !defined;
    }

    std::string a, b;
    if (!args.empty() && args[0] == '(') {
        const size_t close = findClose(args, 1, '(', ')');
        const std::string inner = args.substr(1, close == std::string::npos ? std::string::npos : close - 1);
        const size_t comma = findTopLevel(inner, ',');
        if (comma == std::string::npos) {
            m_st.warn("Malformed conditional '" + kw + " " + args + "' in " + m_cur_file);
            return false;
        }
        a = inner.substr(0, comma);
        b = inner.substr(comma + 1);
    } else {
        // ifeq "a" "b"  /  ifeq 'a' 'b'
        size_t i = 0;
        auto quoted = [&](std::string& out) -> bool {
            while (i < args.size() && isBlank(args[i])) ++i;
            if (i >= args.size() || (args[i] != '"' && args[i] != '\'')) return false;
            const char q = args[i++];
            const size_t e = args.find(q, i);
            if (e == std::string::npos) return false;
            out = args.substr(i, e - i);
            i = e + 1;
            return true;
        };
        if (!quoted(a) || !quoted(b)) {
            m_st.warn("Malformed conditional '" + kw + " " + args + "' in " + m_cur_file);
            return false;
        }
    }
    const bool eq = trim(expand(a)) == trim(expand(b));
    return (kw == "ifeq") ? eq : !eq;
}

void MakeReader::assign(const std::string& name, const std::string& op, const std::string& value,
                        bool is_override, bool is_export)
{
    if (name.empty()) return;

    auto it = m_vars.find(name);
    if (it != m_vars.end() && !is_override &&
        (it->second.origin == "command line" || it->second.origin == "override")) {
        if (is_export) it->second.exported = true;
        return;
    }

    const std::string origin = is_override ? "override" : "file";

    if (op == "=") {
        Var& v = m_vars[name];
        v.value = value;
        v.recursive = true;
        v.origin = origin;
    } else if (op == ":=" || op == "::=" || op == ":::=") {
        const std::string val = expand(value);
        Var& v = m_vars[name];
        v.value = val;
        v.recursive = false;
        v.origin = origin;
    } else if (op == "?=") {
        if (!isDefined(name)) {
            Var& v = m_vars[name];
            v.value = value;
            v.recursive = true;
            v.origin = origin;
        }
    } else if (op == "+=") {
        if (it == m_vars.end()) {
            Var v;
            if (const char* e = std::getenv(name.c_str())) {
                v.value = e;
                v.origin = "environment";
            }
            v.recursive = true;
            it = m_vars.emplace(name, v).first;
        }
        Var& v = it->second;
        const std::string add = v.recursive ? value : expand(value);
        if (!add.empty()) v.value += (v.value.empty() ? "" : " ") + add;
        if (v.origin == "default" || v.origin == "environment") v.origin = origin;
    } else if (op == "!=") {
        const std::string val = shell(expand(value));
        Var& v = m_vars[name];
        v.value = val;
        v.recursive = false;
        v.origin = origin;
    }

    if (is_export) m_vars[name].exported = true;
    if (name == ".RECIPEPREFIX") {
        const std::string rp = m_vars[name].value;
        m_recipe_prefix = rp.empty() ? '\t' : rp[0];
    }
}

// Classify an unexpanded line: 'a' = assignment (op at `pos`), 'r' = rule
// (colon at `pos`), 0 = neither.
char classify(const std::string& t, size_t& pos, std::string& op)
{
    int depth = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '$' && i + 1 < t.size() && (t[i + 1] == '(' || t[i + 1] == '{')) {
            ++depth; ++i; continue;
        }
        if (c == '(' || c == '{') { if (depth > 0) ++depth; continue; }
        if ((c == ')' || c == '}') && depth > 0) { --depth; continue; }
        if (depth > 0) continue;

        if (c == '=') {
            if (i > 0 && (t[i - 1] == '+' || t[i - 1] == '?' || t[i - 1] == '!')) {
                pos = i - 1;
                op = t.substr(i - 1, 2);
            } else {
                pos = i;
                op = "=";
            }
            return 'a';
        }
        if (c == ':') {
            // Windows drive letter: C:/ or C:\ at the very start
            if (i == 1 && std::isalpha((unsigned char)t[0]) && i + 1 < t.size() &&
                (t[i + 1] == '/' || t[i + 1] == '\\'))
                continue;
            size_t n = 1;
            while (i + n < t.size() && t[i + n] == ':') ++n;
            if (i + n < t.size() && t[i + n] == '=' && n <= 3) {
                pos = i;
                op = t.substr(i, n + 1);
                return 'a';
            }
            pos = i;
            return 'r';
        }
    }
    return 0;
}

void MakeReader::processLine(const std::string& line, std::vector<int>& out_rules, int reparse)
{
    std::string t = line;

    //  Directives
    if (isDirective(t, "include") || isDirective(t, "-include") || isDirective(t, "sinclude")) {
        const bool must = t[0] == 'i';
        const size_t kw = t.find_first_of(" \t");
        const std::string rest = kw == std::string::npos ? "" : t.substr(kw);
        for (const auto& f : words(expand(rest))) {
            if (hasGlob(f)) {
                for (const auto& g : wildcard(f)) readMakefile(absUnder(m_cwd, g), must);
            } else {
                readMakefile(absUnder(m_cwd, f), must);
            }
        }
        return;
    }
    if (isDirective(t, "vpath")) {
        const auto w = words(expand(t.substr(5)));
        if (w.empty()) {
            m_vpaths.clear();
        } else if (w.size() == 1) {
            m_vpaths.erase(std::remove_if(m_vpaths.begin(), m_vpaths.end(),
                               [&](const auto& vp) { return vp.first == w[0]; }),
                           m_vpaths.end());
        } else {
            std::vector<std::string> dirs;
            for (size_t k = 1; k < w.size(); ++k) {
                std::string d = w[k];
#ifndef _WIN32
                std::replace(d.begin(), d.end(), ':', ' ');
#else
                std::replace(d.begin(), d.end(), ';', ' ');
#endif
                for (const auto& x : words(d)) dirs.push_back(x);
            }
            m_vpaths.emplace_back(w[0], dirs);
        }
        return;
    }
    if (isDirective(t, "undefine")) {
        std::string rest = trim(t.substr(8));
        if (isDirective(rest, "override")) rest = trim(rest.substr(8));
        m_vars.erase(trim(expand(rest)));
        return;
    }
    if (isDirective(t, "unexport")) {
        const std::string rest = trim(t.substr(8));
        if (rest.empty()) m_export_all = false;
        for (const auto& n : words(expand(rest)))
            if (auto it = m_vars.find(n); it != m_vars.end()) it->second.exported = false;
        return;
    }
    if (isDirective(t, "load") || isDirective(t, "-load")) return;

    bool is_override = false, is_export = false;
    for (;;) {
        if (isDirective(t, "override")) { is_override = true; t = ltrim(t.substr(8)); continue; }
        if (isDirective(t, "export"))   { is_export = true;   t = ltrim(t.substr(6)); continue; }
        if (isDirective(t, "private"))  { t = ltrim(t.substr(7)); continue; }
        break;
    }

    size_t pos = 0;
    std::string op;
    const char kind = classify(t, pos, op);

    if (kind == 'a') {
        const std::string name = trim(expand(t.substr(0, pos)));
        const std::string value = ltrim(t.substr(pos + op.size()));
        assign(name, op, value, is_override, is_export);
        return;
    }

    if (is_export && kind == 0) {
        if (t.empty()) { m_export_all = true; return; }
        for (const auto& n : words(expand(t))) {
            auto it = m_vars.find(n);
            if (it == m_vars.end()) {
                Var v;
                v.value = "";
                it = m_vars.emplace(n, v).first;
            }
            it->second.exported = true;
        }
        return;
    }

    if (kind == 'r') {
        parseRule(t, pos, out_rules);
        return;
    }

    // Neither: a bare function call such as $(eval ...) / $(foreach ...,$(eval ...)).
    // Expanding performs its side effects; anything left over may itself be a rule.
    const std::string e = trim(expand(t));
    if (!e.empty() && reparse < 2) {
        size_t p2;
        std::string op2;
        if (classify(e, p2, op2) != 0)
            processLine(e, out_rules, reparse + 1);
        else
            m_st.warn("Ignored line in " + m_cur_file + ": " + t.substr(0, 60));
    }
}

void MakeReader::parseRule(const std::string& t, size_t colon, std::vector<int>& out_rules)
{
    const std::string tgt_text = t.substr(0, colon);
    const bool dbl = colon + 1 < t.size() && t[colon + 1] == ':';
    std::string rest = t.substr(colon + (dbl ? 2 : 1));

    // wmake implicit-rule search path:  .cpp : dir1;dir2;dir3
    if (m_watcom) {
        const auto tw = words(expand(tgt_text));
        if (tw.size() == 1 && tw[0].size() > 1 && tw[0][0] == '.' && tw[0].find('.', 1) == std::string::npos &&
            std::all_of(tw[0].begin() + 1, tw[0].end(), [](char c) { return std::isalnum((unsigned char)c) || c == '_'; })) {
            std::string dirs = expand(rest);
            std::replace(dirs.begin(), dirs.end(), ';', ' ');
            auto& list = m_ext_paths[lower(tw[0])];
            for (const auto& d : words(dirs)) list.push_back(slashes(d));
            return;
        }
    }

    std::string inline_recipe;
    bool has_inline = false;
    if (size_t semi = findTopLevel(rest, ';'); semi != std::string::npos) {
        inline_recipe = ltrim(rest.substr(semi + 1));
        rest = rest.substr(0, semi);
        has_inline = true;
    }

    // Target-specific variable assignment:  foo.o: CFLAGS += -O2
    {
        std::string r = ltrim(rest);
        for (const char* kw : {"override", "export", "private"})
            if (isDirective(r, kw)) r = ltrim(r.substr(std::strlen(kw)));
        size_t p;
        std::string op;
        if (classify(r, p, op) == 'a') return;
    }

    std::vector<std::string> targets = words(expand(tgt_text));
    if (targets.empty()) return;

    // Static pattern rule?  targets : target-pattern : prereq-patterns
    std::string target_pat;
    bool is_static = false;
    std::string prereq_text = rest;
    {
        size_t p;
        std::string op;
        if (classify(rest, p, op) == 'r') {
            is_static = true;
            target_pat = trim(expand(rest.substr(0, p)));
            prereq_text = rest.substr(p + 1);
        }
    }

    std::vector<std::string> pre, order;
    {
        bool oo = false;
        for (auto& w : words(expand(prereq_text))) {
            if (w == "|") { oo = true; continue; }
            (oo ? order : pre).push_back(w);
        }
    }

    if (targets[0] == ".PHONY") {
        for (auto& p : pre) m_phony.insert(p);
        return;
    }
    if (m_watcom) {
        // Dependency attributes:  clean : .symbolic   /  .SYMBOLIC : clean
        static const std::set<std::string> attrs = {
            ".symbolic", ".procedure", ".always", ".precious", ".multiple", ".silent", ".ignore",
            ".existsonly", ".explicit", ".hold", ".just_enough", ".recheck", ".autodepend", ".nocheck"};
        bool phony = false;
        std::vector<std::string> kept;
        for (auto& p : pre) {
            const std::string lp = lower(p);
            if (attrs.count(lp)) { phony |= (lp == ".symbolic" || lp == ".procedure"); continue; }
            kept.push_back(slashes(p));
        }
        pre = std::move(kept);
        for (auto& p : order) p = slashes(p);
        if (phony) for (auto& tg : targets) m_phony.insert(tg);
        const std::string lt = lower(targets[0]);
        if (lt == ".symbolic" || lt == ".procedure") {
            for (auto& p : pre) m_phony.insert(p);
            return;
        }
    }
    if (targets[0] == ".EXPORT_ALL_VARIABLES") m_export_all = true;

    // Old-fashioned suffix rule  .c.o:  →  %.o: %.c
    if (!is_static && targets.size() == 1 && pre.empty() && targets[0].size() > 2 && targets[0][0] == '.') {
        const std::string& s = targets[0];
        const size_t second = s.find('.', 1);
        if (second != std::string::npos && s.find('.', second + 1) == std::string::npos) {
            const std::string from = s.substr(0, second), to = s.substr(second);
            const auto known = words(lookup("SUFFIXES"));
            auto has = [&](const std::string& x) {
                return m_watcom || std::find(known.begin(), known.end(), x) != known.end();
            };
            if (has(from) && has(to)) {
                targets = {"%" + to};
                pre = {"%" + from};
            }
        }
    }

    if (is_static) {
        for (const auto& tg : targets) {
            std::string stem;
            if (!patMatch(target_pat, tg, &stem)) {
                m_st.warn("Target '" + tg + "' doesn't match pattern '" + target_pat + "'");
                continue;
            }
            Rule r;
            r.targets = {tg};
            r.stem = stem;
            for (auto& p : pre)   r.prereqs.push_back(patFill(p, stem));
            for (auto& p : order) r.order_only.push_back(patFill(p, stem));
            m_rules.push_back(std::move(r));
            out_rules.push_back((int)m_rules.size() - 1);
        }
    } else {
        Rule r;
        r.targets = targets;
        r.prereqs = pre;
        r.order_only = order;
        r.pattern = std::any_of(targets.begin(), targets.end(),
                                [](const std::string& x) { return x.find('%') != std::string::npos; });
        m_rules.push_back(std::move(r));
        out_rules.push_back((int)m_rules.size() - 1);
    }

    if (has_inline && !inline_recipe.empty())
        for (int idx : out_rules) m_rules[(size_t)idx].recipe.push_back(inline_recipe);

    // Default goal: first target of the first ordinary rule
    if (m_default_goal.empty() && !out_rules.empty()) {
        const Rule& r = m_rules[(size_t)out_rules.front()];
        const std::string& g = r.targets.front();
        const bool special = g[0] == '.' && g.find('/') == std::string::npos;
        if (!r.pattern && !special) m_default_goal = g;
    }
}

//  Expansion

static const std::unordered_map<std::string, int> kFunctions = {
    {"subst", 3}, {"patsubst", 3}, {"strip", 1}, {"findstring", 2}, {"filter", 2},
    {"filter-out", 2}, {"sort", 1}, {"word", 2}, {"wordlist", 3}, {"words", 1},
    {"firstword", 1}, {"lastword", 1}, {"dir", 1}, {"notdir", 1}, {"suffix", 1},
    {"basename", 1}, {"addsuffix", 2}, {"addprefix", 2}, {"join", 2}, {"wildcard", 1},
    {"realpath", 1}, {"abspath", 1}, {"if", 3}, {"or", -1}, {"and", -1}, {"foreach", 3},
    {"call", -1}, {"value", 1}, {"eval", 1}, {"origin", 1}, {"flavor", 1}, {"shell", 1},
    {"info", 1}, {"warning", 1}, {"error", 1}, {"file", 1}, {"let", 3}, {"intcmp", 5},
};

std::string MakeReader::expand(const std::string& s)
{
    if (s.find('$') == std::string::npos) return s;
    if (m_expand_depth > 400) return "";
    ++m_expand_depth;

    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c != '$') { out += c; continue; }
        if (i + 1 >= s.size()) break;
        const char n = s[i + 1];
        if (n == '$') { out += '$'; ++i; continue; }
        if (m_watcom) {
            if (n == '+' || n == '-') { ++i; continue; }      // wmake immediate-expansion toggles
            // $[@ $[* $[& $[: $[.  (first dependent)   $]x (last)   $^x (target)
            if ((n == '[' || n == ']' || n == '^') && i + 2 < s.size() &&
                std::strchr("@*&:.", s[i + 2]) != nullptr) {
                const std::string file = lookup(n == '^' ? "@" : std::string(1, n));
                out += wmakeFileMacro(file, s[i + 2]);
                i += 2;
                continue;
            }
        }
        if (n == '(' || n == '{') {
            const char close = (n == '(') ? ')' : '}';
            const size_t end = findClose(s, i + 2, n, close);
            if (end == std::string::npos) {
                m_st.warn("Unterminated variable reference in " + m_cur_file);
                break;
            }
            out += expandRef(s.substr(i + 2, end - i - 2));
            i = end;
            continue;
        }
        out += lookup(std::string(1, n));
        ++i;
    }

    --m_expand_depth;
    return out;
}

std::string MakeReader::expandRef(const std::string& inner)
{
    //  Function call
    const size_t ws = inner.find_first_of(" \t");
    if (ws != std::string::npos) {
        auto it = kFunctions.find(inner.substr(0, ws));
        if (it != kFunctions.end()) {
            const size_t a = inner.find_first_not_of(" \t", ws);
            const std::string args = (a == std::string::npos) ? "" : inner.substr(a);
            return callFunction(it->first, splitArgs(args, it->second));
        }
    }

    //  Substitution reference  $(VAR:a=b) / $(VAR:%.c=%.o)
    const size_t colon = findTopLevel(inner, ':');
    if (colon != std::string::npos) {
        const size_t eq = findTopLevel(inner, '=', colon + 1);
        if (eq != std::string::npos) {
            const std::string name = trim(expand(inner.substr(0, colon)));
            std::string from = trim(expand(inner.substr(colon + 1, eq - colon - 1)));
            std::string to   = trim(expand(inner.substr(eq + 1)));
            if (from.find('%') == std::string::npos) {
                from = "%" + from;
                to = "%" + to;
            }
            std::vector<std::string> out;
            for (const auto& w : words(lookup(name))) out.push_back(patSubstWord(from, to, w));
            return join(out);
        }
    }

    const std::string name = expand(inner);

    //  $(@D) $(@F) $(<D) ...
    if (name.size() == 2 && std::strchr("@<^+*?%|", name[0]) && (name[1] == 'D' || name[1] == 'F')) {
        std::vector<std::string> out;
        for (const auto& w : words(lookup(name.substr(0, 1)))) {
            const size_t sl = w.rfind('/');
            if (name[1] == 'D') out.push_back(sl == std::string::npos ? "." : (sl == 0 ? "/" : w.substr(0, sl)));
            else                out.push_back(sl == std::string::npos ? w : w.substr(sl + 1));
        }
        return join(out);
    }
    return lookup(name);
}

std::string MakeReader::lookup(const std::string& name)
{
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return f->second;
    }
    auto v = m_vars.find(name);
    if (v != m_vars.end()) {
        if (!v->second.recursive) return v->second.value;
        if (m_expanding.count(name)) {
            m_st.warn("Recursive variable '" + name + "' references itself");
            return "";
        }
        m_expanding.insert(name);
        const std::string value = v->second.value;   // map may rehash during expansion
        std::string r = expand(value);
        m_expanding.erase(name);
        return r;
    }
    if (name.empty() || name.find_first_of(" \t=") != std::string::npos) return "";
    if (m_watcom) {
        if (name[0] == '%') {                                 // $(%path): environment variable
            const char* e = std::getenv(name.c_str() + 1);
            return e ? e : "";
        }
        const std::string ln = lower(name);                   // DOS makefiles aren't consistent
        for (const auto& [n, var] : m_vars)
            if (lower(n) == ln) return lookup(n);
    }
    if (const char* e = std::getenv(name.c_str())) return e;
    return "";
}

// DOS paths to '/', keeping \" (an escaped quote in -DX=\"y\").
std::string dosSlashes(std::string s)
{
#ifndef _WIN32
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == '\\' && (i + 1 >= s.size() || s[i + 1] != '"')) s[i] = '/';
#endif
    return s;
}

// wmake file-name macro modifiers:  @ full   * path+name   & name   : path   . name+ext
std::string wmakeFileMacro(const std::string& f, char mod)
{
    const size_t sl = f.find_last_of("/\\");
    const std::string dir  = sl == std::string::npos ? "" : f.substr(0, sl + 1);
    const std::string file = sl == std::string::npos ? f : f.substr(sl + 1);
    const size_t dot = file.rfind('.');
    const std::string stem = dot == std::string::npos ? file : file.substr(0, dot);
    switch (mod) {
    case '*': return dir + stem;
    case '&': return stem;
    case ':': return dir;
    case '.': return file;
    default:  return f;
    }
}

std::string MakeReader::shell(const std::string& cmd)
{
    if (m_no_shell || !m_st.opts.allow_shell) return "";
    const std::string c = trim(cmd);
    if (c.empty()) return "";
    return runShell(m_st, c, m_cwd);
}

std::vector<std::string> MakeReader::wildcard(const std::string& pattern)
{
    std::vector<std::string> results;
    if (pattern.empty()) return results;

    if (!hasGlob(pattern)) {
        std::error_code ec;
        if (fs::exists(absUnder(m_cwd, pattern), ec)) results.push_back(pattern);
        return results;
    }

    // Walk component by component; keep the spelling the user wrote.
    std::vector<std::string> comps;
    {
        std::string cur;
        for (char c : pattern) {
            if (c == '/') { comps.push_back(cur); cur.clear(); }
            else cur += c;
        }
        comps.push_back(cur);
    }

    struct Partial { std::string spelled; fs::path abs; };
    std::vector<Partial> parts;
    const bool absolute = fs::path(pattern).is_absolute();
    size_t first = 0;
    if (!pattern.empty() && pattern[0] == '/') {
        parts.push_back({"/", fs::path("/")});
        first = 1;
    } else if (absolute) {                      // C:/...
        parts.push_back({comps[0] + "/", fs::path(comps[0] + "/")});
        first = 1;
    } else {
        parts.push_back({"", m_cwd});
    }

    for (size_t k = first; k < comps.size() && !parts.empty(); ++k) {
        const std::string& comp = comps[k];
        const bool last = (k + 1 == comps.size());
        std::vector<Partial> next;
        for (const auto& p : parts) {
            auto sep = [&](const std::string& s) {
                return (p.spelled.empty() || p.spelled.back() == '/') ? p.spelled + s : p.spelled + "/" + s;
            };
            if (comp.empty()) {                  // "a//b" or trailing '/'
                if (!last) next.push_back(p);
                else next.push_back({sep(""), p.abs});
                continue;
            }
            if (!hasGlob(comp)) {
                const fs::path q = p.abs / comp;
                std::error_code ec;
                if (last ? fs::exists(q, ec) : fs::is_directory(q, ec))
                    next.push_back({sep(comp), q});
                continue;
            }
            std::error_code ec;
            std::vector<std::string> names;
            for (fs::directory_iterator it(p.abs, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                const std::string n = it->path().filename().string();
                if (n[0] == '.' && comp[0] != '.') continue;
                if (!globMatch(comp.c_str(), n.c_str())) continue;
                std::error_code ec2;
                if (!last && !it->is_directory(ec2)) continue;
                names.push_back(n);
            }
            std::sort(names.begin(), names.end());
            for (const auto& n : names) next.push_back({sep(n), p.abs / n});
        }
        parts = std::move(next);
    }

    for (const auto& p : parts) results.push_back(p.spelled);
    std::sort(results.begin(), results.end());
    return results;
}

std::string MakeReader::callFunction(const std::string& fn, const std::vector<std::string>& args)
{
    auto A = [&](size_t i) { return i < args.size() ? expand(args[i]) : std::string(); };
    auto wordsOf = [&](size_t i) { return words(A(i)); };

    if (fn == "subst") {
        const std::string from = A(0), to = A(1), text = A(2);
        return replaceAll(text, from, to);
    }
    if (fn == "patsubst") {
        const std::string pat = trim(A(0)), rep = trim(A(1));
        std::vector<std::string> out;
        for (const auto& w : wordsOf(2)) out.push_back(patSubstWord(pat, rep, w));
        return join(out);
    }
    if (fn == "strip") return join(wordsOf(0));
    if (fn == "findstring") {
        const std::string f = A(0), in = A(1);
        return in.find(f) != std::string::npos ? f : "";
    }
    if (fn == "filter" || fn == "filter-out") {
        const auto pats = wordsOf(0);
        const bool keep = (fn == "filter");
        std::vector<std::string> out;
        for (const auto& w : wordsOf(1)) {
            const bool m = std::any_of(pats.begin(), pats.end(), [&](const std::string& p) { return patMatch(p, w); });
            if (m == keep) out.push_back(w);
        }
        return join(out);
    }
    if (fn == "sort") {
        auto w = wordsOf(0);
        std::sort(w.begin(), w.end());
        w.erase(std::unique(w.begin(), w.end()), w.end());
        return join(w);
    }
    if (fn == "word") {
        const auto w = wordsOf(1);
        long n = std::strtol(trim(A(0)).c_str(), nullptr, 10);
        return (n >= 1 && n <= (long)w.size()) ? w[(size_t)n - 1] : "";
    }
    if (fn == "wordlist") {
        long s = std::strtol(trim(A(0)).c_str(), nullptr, 10);
        long e = std::strtol(trim(A(1)).c_str(), nullptr, 10);
        const auto w = wordsOf(2);
        std::vector<std::string> out;
        for (long i = std::max(1L, s); i <= e && i <= (long)w.size(); ++i) out.push_back(w[(size_t)i - 1]);
        return join(out);
    }
    if (fn == "words") return std::to_string(wordsOf(0).size());
    if (fn == "firstword") { auto w = wordsOf(0); return w.empty() ? "" : w.front(); }
    if (fn == "lastword")  { auto w = wordsOf(0); return w.empty() ? "" : w.back(); }
    if (fn == "dir" || fn == "notdir") {
        std::vector<std::string> out;
        for (const auto& w : wordsOf(0)) {
            const size_t sl = w.rfind('/');
            if (fn == "dir") out.push_back(sl == std::string::npos ? "./" : w.substr(0, sl + 1));
            else {
                std::string n = sl == std::string::npos ? w : w.substr(sl + 1);
                if (!n.empty()) out.push_back(n);
            }
        }
        return join(out);
    }
    if (fn == "suffix" || fn == "basename") {
        std::vector<std::string> out;
        for (const auto& w : wordsOf(0)) {
            const size_t sl = w.rfind('/');
            const size_t dot = w.rfind('.');
            const bool has = dot != std::string::npos && (sl == std::string::npos || dot > sl);
            if (fn == "suffix") { if (has) out.push_back(w.substr(dot)); }
            else out.push_back(has ? w.substr(0, dot) : w);
        }
        return join(out);
    }
    if (fn == "addsuffix" || fn == "addprefix") {
        const std::string x = A(0);
        std::vector<std::string> out;
        for (const auto& w : wordsOf(1)) out.push_back(fn == "addsuffix" ? w + x : x + w);
        return join(out);
    }
    if (fn == "join") {
        const auto a = wordsOf(0), b = wordsOf(1);
        std::vector<std::string> out;
        for (size_t i = 0; i < std::max(a.size(), b.size()); ++i)
            out.push_back((i < a.size() ? a[i] : "") + (i < b.size() ? b[i] : ""));
        return join(out);
    }
    if (fn == "wildcard") {
        std::vector<std::string> out;
        for (const auto& w : wordsOf(0))
            for (auto& r : wildcard(w)) out.push_back(std::move(r));
        return join(out);
    }
    if (fn == "realpath" || fn == "abspath") {
        std::vector<std::string> out;
        for (const auto& w : wordsOf(0)) {
            fs::path p = absUnder(m_cwd, w);
            if (fn == "realpath") {
                std::error_code ec;
                if (!fs::exists(p, ec)) continue;
                p = fs::weakly_canonical(p, ec);
            }
            std::string s = p.generic_string();
            if (s.size() > 1 && s.back() == '/') s.pop_back();
            out.push_back(s);
        }
        return join(out);
    }
    if (fn == "if") {
        if (!trim(A(0)).empty()) return A(1);
        return A(2);
    }
    if (fn == "or") {
        for (size_t i = 0; i < args.size(); ++i) {
            std::string v = trim(expand(args[i]));
            if (!v.empty()) return v;
        }
        return "";
    }
    if (fn == "and") {
        std::string last;
        for (size_t i = 0; i < args.size(); ++i) {
            last = trim(expand(args[i]));
            if (last.empty()) return "";
        }
        return last;
    }
    if (fn == "foreach") {
        const std::string var = trim(A(0));
        const auto list = wordsOf(1);
        const std::string body = args.size() > 2 ? args[2] : "";
        std::vector<std::string> out;
        for (const auto& w : list) {
            m_scopes.push_back({{var, w}});
            std::string r = expand(body);
            m_scopes.pop_back();
            if (!r.empty()) out.push_back(std::move(r));
        }
        return join(out);
    }
    if (fn == "call") {
        const std::string name = trim(A(0));
        if (m_call_depth > 64) {
            m_st.warn("$(call " + name + ") recursion too deep");
            return "";
        }
        std::unordered_map<std::string, std::string> scope;
        scope["0"] = name;
        for (size_t i = 1; i < std::max<size_t>(args.size(), 10); ++i)
            scope[std::to_string(i)] = i < args.size() ? expand(args[i]) : "";
        auto v = m_vars.find(name);
        if (v == m_vars.end()) return "";
        const Var var = v->second;
        ++m_call_depth;
        m_scopes.push_back(std::move(scope));
        std::string r = var.recursive ? expand(var.value) : var.value;
        m_scopes.pop_back();
        --m_call_depth;
        return r;
    }
    if (fn == "value") {
        const std::string name = trim(A(0));
        auto v = m_vars.find(name);
        if (v != m_vars.end()) return v->second.value;
        const char* e = std::getenv(name.c_str());
        return e ? e : "";
    }
    if (fn == "eval") {
        if (m_analysis) return "";
        if (m_eval_depth > 32) return "";
        const std::string text = A(0);
        std::vector<std::string> lines;
        std::stringstream ss(text);
        std::string ln;
        while (std::getline(ss, ln)) lines.push_back(ln);
        ++m_eval_depth;
        parseLines(lines);
        --m_eval_depth;
        return "";
    }
    if (fn == "origin" || fn == "flavor") {
        const std::string name = trim(A(0));
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it)
            if (it->count(name)) return fn == "origin" ? "automatic" : "simple";
        auto v = m_vars.find(name);
        if (v != m_vars.end())
            return fn == "origin" ? v->second.origin : (v->second.recursive ? "recursive" : "simple");
        if (std::getenv(name.c_str())) return fn == "origin" ? "environment" : "recursive";
        return "undefined";
    }
    if (fn == "shell") return shell(A(0));
    if (fn == "info" || fn == "warning") { (void)A(0); return ""; }
    if (fn == "error") {
        const std::string msg = A(0);
        if (!m_analysis) m_st.warn("$(error " + msg + ") in " + m_cur_file + " (ignored)");
        return "";
    }
    if (fn == "file") {
        const std::string a = trim(A(0));
        if (a.empty() || a[0] != '<') return "";
        std::ifstream f(absUnder(m_cwd, trim(a.substr(1))), std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(f)), {});
        if (!content.empty() && content.back() == '\n') content.pop_back();
        return content;
    }
    if (fn == "let") {
        const auto names = wordsOf(0);
        const auto list = wordsOf(1);
        std::unordered_map<std::string, std::string> scope;
        for (size_t i = 0; i < names.size(); ++i) {
            if (i + 1 == names.size()) {
                std::vector<std::string> restw(list.begin() + (long)std::min(i, list.size()), list.end());
                scope[names[i]] = join(restw);
            } else {
                scope[names[i]] = i < list.size() ? list[i] : "";
            }
        }
        m_scopes.push_back(std::move(scope));
        std::string r = args.size() > 2 ? expand(args[2]) : "";
        m_scopes.pop_back();
        return r;
    }
    if (fn == "intcmp") {
        const long a = std::strtol(trim(A(0)).c_str(), nullptr, 10);
        const long b = std::strtol(trim(A(1)).c_str(), nullptr, 10);
        if (args.size() <= 2) return a == b ? std::to_string(a) : "";
        if (a < b) return A(2);
        if (a == b) return args.size() > 3 ? A(3) : A(2);
        return args.size() > 4 ? A(4) : (args.size() > 3 ? A(3) : A(2));
    }
    return "";
}

//  Analysis

void MakeReader::buildIndexes()
{
    m_explicit.clear();
    m_pattern_rules.clear();
    for (size_t i = 0; i < m_rules.size(); ++i) {
        const Rule& r = m_rules[i];
        if (r.pattern) { m_pattern_rules.push_back((int)i); continue; }
        for (const auto& t : r.targets) m_explicit[t].push_back((int)i);
    }
}

fs::path MakeReader::resolve(const std::string& name_in)
{
    if (name_in.empty()) return {};
    auto c = m_resolve_cache.find(name_in);
    if (c != m_resolve_cache.end()) return c->second;

    const std::string name = m_watcom ? slashes(name_in) : name_in;
    // wmake projects come from case-insensitive file systems
    auto tryFile = [&](const fs::path& q) -> fs::path {
        if (isRegularFile(q)) return q;
        if (m_watcom)
            if (fs::path r = findNoCase(q); !r.empty() && isRegularFile(r)) return r;
        return {};
    };

    fs::path found = tryFile(absUnder(m_cwd, name));
    const fs::path p(name);
    if (found.empty() && !p.is_absolute()) {
        for (const auto& [pat, dirs] : m_vpaths) {
            if (!patMatch(pat, name)) continue;
            for (const auto& d : dirs)
                if (found = tryFile((absUnder(m_cwd, d) / name).lexically_normal()); !found.empty()) break;
            if (!found.empty()) break;
        }
        if (found.empty()) {
            std::string vp = lookup("VPATH");
#ifndef _WIN32
            std::replace(vp.begin(), vp.end(), ':', ' ');
#else
            std::replace(vp.begin(), vp.end(), ';', ' ');
#endif
            for (const auto& d : words(vp))
                if (found = tryFile((absUnder(m_cwd, d) / name).lexically_normal()); !found.empty()) break;
        }
        // wmake:  .cpp : dir1;dir2  - where files of that extension are looked for
        if (found.empty()) {
            auto ep = m_ext_paths.find(lower(extOf(name)));
            if (ep != m_ext_paths.end())
                for (const auto& d : ep->second)
                    if (found = tryFile((absUnder(m_cwd, d) / name).lexically_normal()); !found.empty()) break;
        }
    }
    m_resolve_cache.emplace(name_in, found);
    return found;
}

// Source files with a given stem, used as a last resort to map an object file
// like build/obj/foo.o back to its source when no rule spells the mapping out.
void MakeReader::buildStemIndex()
{
    if (m_stem_index_built) return;
    m_stem_index_built = true;

    auto add = [&](const fs::path& p) {
        const std::string stem = p.stem().string();
        auto& v = m_stem_index[stem];
        if (std::find(v.begin(), v.end(), p) == v.end()) v.push_back(p);
    };

    // Everything the makefile names in its variables (can reach parent dirs)
    m_analysis = true;
    for (const auto& [name, v] : std::vector<std::pair<std::string, Var>>(m_vars.begin(), m_vars.end())) {
        if (v.origin == "default" || v.origin == "environment") continue;
        const std::string val = v.recursive ? expand(v.value) : v.value;
        for (const auto& w : words(val))
            if (isSourceFile(w))
                if (auto r = resolve(w); !r.empty()) add(r);
    }
    m_analysis = false;

    // Plus a bounded scan below the working directory
    std::error_code ec;
    size_t budget = 20000;
    for (fs::recursive_directory_iterator it(m_cwd, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end && budget > 0; it.increment(ec), --budget) {
        const std::string fn = it->path().filename().string();
        std::error_code ec2;
        if (it->is_directory(ec2)) {
            if ((!fn.empty() && fn[0] == '.') || it.depth() > 8) it.disable_recursion_pending();
            continue;
        }
        if (isSourceFile(fn)) add(it->path().lexically_normal());
    }
}

// Inputs for an object file: its source first (if found), then header deps.
std::vector<fs::path> MakeReader::objectInputs(const std::string& obj)
{
    std::vector<fs::path> out;
    auto push = [&](const fs::path& p) {
        if (!p.empty() && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    };

    // 1. Explicit rules for the object (hand-written or from generated .d files)
    bool have_src = false;
    if (auto it = m_explicit.find(obj); it != m_explicit.end()) {
        for (int idx : it->second) {
            for (const auto& p : m_rules[(size_t)idx].prereqs) {
                if (isSourceFile(p)) {
                    if (auto r = resolve(p); !r.empty()) {
                        out.insert(out.begin() + (have_src ? (long)out.size() : 0), r);
                        have_src = true;
                    }
                } else if (isHeaderFile(p)) {
                    if (auto r = resolve(p); !r.empty() && !isSystemPath(r)) push(r);
                }
            }
        }
    }
    if (have_src) return out;

    // 2. Pattern rules, most recently defined first
    for (auto it = m_pattern_rules.rbegin(); it != m_pattern_rules.rend(); ++it) {
        const Rule& rule = m_rules[(size_t)*it];
        if (rule.prereqs.empty()) continue;
        for (const auto& tp : rule.targets) {
            std::string name = obj, dir;
            if (tp.find('/') == std::string::npos) {
                const size_t sl = obj.rfind('/');
                if (sl != std::string::npos) { dir = obj.substr(0, sl + 1); name = obj.substr(sl + 1); }
            }
            std::string stem;
            if (!patMatch(tp, name, &stem)) continue;

            std::vector<fs::path> cand;
            bool ok = false;
            for (size_t k = 0; k < rule.prereqs.size(); ++k) {
                const std::string& pp = rule.prereqs[k];
                const std::string file = pp.find('%') != std::string::npos ? dir + patFill(pp, stem) : pp;
                if (isSourceFile(file)) {
                    auto r = resolve(file);
                    if (r.empty()) { if (!ok) break; continue; }
                    cand.insert(cand.begin(), r);
                    ok = true;
                } else if (isHeaderFile(file)) {
                    if (auto r = resolve(file); !r.empty()) cand.push_back(r);
                }
            }
            if (ok) {
                // source first; header deps found in explicit rules follow
                std::vector<fs::path> headers = std::move(out);
                out.clear();
                for (auto& c : cand) push(c);
                for (auto& h : headers) push(h);
                return out;
            }
        }
    }

    // 3. GNU make's built-in rules: foo.o ← foo.c / foo.cc / foo.cpp / ...
    const std::string base = obj.substr(0, obj.size() - extOf(obj).size());
    static const char* builtin_exts[] = {".c", ".cc", ".cpp", ".C", ".cxx", ".c++", ".s", ".S", ".m", ".mm"};
    for (const char* e : builtin_exts) {
        if (auto r = resolve(base + e); !r.empty()) {
            out.insert(out.begin(), r);
            return out;
        }
    }

    // 4. Last resort: a unique source file with the same stem
    buildStemIndex();
    const std::string stem = fs::path(base).filename().string();
    auto hit = m_stem_index.find(stem);
    if (hit != m_stem_index.end() && !hit->second.empty()) {
        if (hit->second.size() == 1) {
            out.insert(out.begin(), hit->second.front());
        } else {
            // Prefer the candidate sharing the longest trailing directory path
            // with the object (build/net/foo.o ↔ src/net/foo.c).
            const fs::path objp = fs::path(base).parent_path();
            std::vector<std::string> oparts;
            for (const auto& c : objp) oparts.push_back(c.string());
            int best = -1, best_score = -1;
            bool tie = false;
            for (size_t k = 0; k < hit->second.size(); ++k) {
                std::vector<std::string> sparts;
                for (const auto& c : hit->second[k].parent_path()) sparts.push_back(c.string());
                int score = 0;
                while (score < (int)oparts.size() && score < (int)sparts.size() &&
                       oparts[oparts.size() - 1 - (size_t)score] == sparts[sparts.size() - 1 - (size_t)score])
                    ++score;
                if (score > best_score) { best_score = score; best = (int)k; tie = false; }
                else if (score == best_score) tie = true;
            }
            if (best >= 0 && !tie) out.insert(out.begin(), hit->second[(size_t)best]);
        }
    }
    return out;
}

std::string MakeReader::expandRecipe(const std::vector<std::string>& recipe, const std::string& target,
                                     const std::vector<std::string>& prereqs,
                                     const std::vector<std::string>& order_only,
                                     const std::string& stem)
{
    std::vector<std::string> uniq;
    for (const auto& p : prereqs)
        if (std::find(uniq.begin(), uniq.end(), p) == uniq.end()) uniq.push_back(p);

    std::unordered_map<std::string, std::string> autos;
    autos["@"] = target;
    autos["<"] = prereqs.empty() ? "" : prereqs.front();
    autos["^"] = join(uniq);
    autos["+"] = join(prereqs);
    autos["?"] = join(uniq);
    autos["|"] = join(order_only);
    autos["*"] = stem;
    autos["%"] = "";
    if (m_watcom) {
        autos["<"] = join(prereqs);                     // wmake: all dependents
        autos["["] = prereqs.empty() ? "" : prereqs.front();
        autos["]"] = prereqs.empty() ? "" : prereqs.back();
    }

    m_scopes.push_back(std::move(autos));
    const bool saved_shell = m_no_shell, saved_an = m_analysis;
    m_no_shell = true;
    m_analysis = true;
    std::string out;
    for (const auto& line : recipe) {
        std::string e = expand(line);
        size_t b = 0;
        while (b < e.size() && (e[b] == '@' || e[b] == '-' || e[b] == '+' || isBlank(e[b]))) ++b;
        out += e.substr(b);
        out += '\n';
    }
    m_no_shell = saved_shell;
    m_analysis = saved_an;
    m_scopes.pop_back();
    return out;
}

// Find `make` invocations in an (expanded) recipe line.
void MakeReader::scanShell(const std::string& cmd_in, const fs::path& dir, std::vector<SubMake>& out, int depth)
{
    if (depth > 4) return;
    std::string cmd = m_watcom ? dosSlashes(cmd_in) : cmd_in;   // DOS commands: '\\' is a separator

    //  DOS:  for %d in (a b c) do wmake -h -f %d/makefile
    if (m_watcom) {
        const std::string lc = lower(cmd);
        const size_t f = lc.find("for %");
        if (f != std::string::npos) {
            size_t v = f + 4;
            while (v < cmd.size() && cmd[v] == '%') ++v;
            size_t ve = v;
            while (ve < cmd.size() && !isBlank(cmd[ve])) ++ve;
            const std::string var = cmd.substr(v, ve - v);
            const size_t open = cmd.find('(', ve), close = cmd.find(')', open == std::string::npos ? ve : open);
            const size_t do_pos = lc.find(" do ", close == std::string::npos ? ve : close);
            if (!var.empty() && open != std::string::npos && close != std::string::npos && do_pos != std::string::npos) {
                std::string list = cmd.substr(open + 1, close - open - 1);
                std::replace(list.begin(), list.end(), ',', ' ');
                const std::string body = cmd.substr(do_pos + 4);
                for (const auto& w : words(list)) {
                    std::string b = replaceAll(body, "%%" + var, w);
                    b = replaceAll(b, "%" + var, w);
                    scanShell(b, dir, out, depth + 1);
                }
                cmd = cmd.substr(0, f);
            }
        }
    }

    //  for VAR in LIST; do BODY; done
    for (;;) {
        const size_t f = cmd.find("for ");
        if (f == std::string::npos || (f > 0 && !isBlank(cmd[f - 1]) && std::strchr(";&|(", cmd[f - 1]) == nullptr))
            break;
        const size_t in_pos = cmd.find(" in ", f);
        if (in_pos == std::string::npos) break;
        const size_t semi = cmd.find(';', in_pos);
        if (semi == std::string::npos) break;
        size_t do_pos = semi + 1;
        while (do_pos < cmd.size() && isBlank(cmd[do_pos])) ++do_pos;
        if (cmd.compare(do_pos, 2, "do") != 0 || do_pos + 2 >= cmd.size() || !isBlank(cmd[do_pos + 2]))
            break;
        const size_t done = cmd.rfind("done");
        if (done == std::string::npos || done < do_pos) break;
        const std::string var  = trim(cmd.substr(f + 4, in_pos - f - 4));
        const std::string list = cmd.substr(in_pos + 4, semi - in_pos - 4);
        const std::string body = cmd.substr(do_pos + 3, done - do_pos - 3);
        for (const auto& w : shellSplit(list)) {
            std::string b = replaceAll(body, "${" + var + "}", w);
            b = replaceAll(b, "$" + var, w);
            scanShell(b, dir, out, depth + 1);
        }
        cmd = cmd.substr(0, f) + " ; " + cmd.substr(done + 4);
    }

    //  Split into simple commands, tracking `cd`
    std::vector<std::string> cmds;
    {
        std::string cur;
        char quote = 0;
        for (size_t i = 0; i < cmd.size(); ++i) {
            const char c = cmd[i];
            if (quote) { cur += c; if (c == quote) quote = 0; continue; }
            if (c == '\'' || c == '"') { quote = c; cur += c; continue; }
            if (c == ';' || c == '\n' || c == '&' || c == '|' || c == '(' || c == ')') {
                cmds.push_back(cur);
                cur.clear();
                continue;
            }
            cur += c;
        }
        cmds.push_back(cur);
    }

    const std::string make_var = trim(lookup("MAKE"));
    fs::path cur_dir = dir;
    for (const auto& c : cmds) {
        auto toks = shellSplit(c);
        size_t k = 0;
        // leading VAR=value environment assignments
        while (k < toks.size() && toks[k].find('=') != std::string::npos && toks[k][0] != '-') ++k;
        if (k >= toks.size()) continue;

        const std::string& prog = toks[k];
        if (prog == "cd" || prog == "pushd" || (m_watcom && lower(prog) == "cd")) {
            size_t a = k + 1;
            if (a < toks.size() && lower(toks[a]) == "/d") ++a;   // cmd.exe: cd /d dir
            if (a < toks.size()) cur_dir = absUnder(cur_dir, toks[a]);
            continue;
        }
        if (prog == "exec" || prog == "env" || prog == "command") ++k;
        if (k >= toks.size()) continue;

        std::string base = fs::path(toks[k]).filename().string();
        std::transform(base.begin(), base.end(), base.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
        if (endsWith(base, ".exe")) base.resize(base.size() - 4);
        const bool is_make = base == "make" || base == "gmake" || base == "mingw32-make" || base == "remake" ||
                             base == "wmake" || (!make_var.empty() && toks[k] == make_var);
        if (!is_make) continue;

        SubMake sm;
        sm.dir = cur_dir;
        sm.watcom = base == "wmake" || (m_watcom && toks[k] == make_var);
        for (size_t i = k + 1; i < toks.size(); ++i) {
            const std::string& a = toks[i];
            auto next = [&]() -> std::string { return i + 1 < toks.size() ? toks[++i] : std::string(); };
            if (a == "-C" || a == "--directory")           sm.dir = absUnder(sm.dir, next());
            else if (startsWith(a, "--directory="))          sm.dir = absUnder(sm.dir, a.substr(12));
            else if (startsWith(a, "-C"))                    sm.dir = absUnder(sm.dir, a.substr(2));
            else if (a == "-f" || a == "--file" || a == "--makefile") sm.file = next();
            else if (startsWith(a, "--file="))               sm.file = a.substr(7);
            else if (startsWith(a, "--makefile="))           sm.file = a.substr(11);
            else if (startsWith(a, "-f") && a.size() > 2)    sm.file = a.substr(2);
            else if (a == "-I" || a == "-o" || a == "-W" || a == "--include-dir" ||
                     a == "--old-file" || a == "--what-if" || a == "--assume-new") (void)next();
            else if (a == "-j" || a == "-l") {
                if (i + 1 < toks.size() && std::isdigit((unsigned char)toks[i + 1][0])) ++i;
            }
            else if (a[0] != '-' && a.find('=') != std::string::npos) {
                const size_t eq = a.find('=');
                std::string name = a.substr(0, eq);
                if (!name.empty() && (name.back() == ':' || name.back() == '+' || name.back() == '?'))
                    name.pop_back();
                sm.vars.emplace_back(trim(name), a.substr(eq + 1));
            }
        }
        out.push_back(std::move(sm));
    }
}

std::vector<SubMake> MakeReader::findSubMakes()
{
    std::vector<SubMake> out;
    for (const Rule& r : m_rules) {
        if (r.pattern || r.recipe.empty()) continue;
        bool mentions = false;
        for (const auto& l : r.recipe) {
            std::string low = l;
            std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (low.find("make") != std::string::npos) { mentions = true; break; }
        }
        if (!mentions) continue;

        size_t n = 0;
        for (const auto& tg : r.targets) {
            if (++n > 64) break;
            const std::string text = expandRecipe(r.recipe, tg, r.prereqs, r.order_only, r.stem);
            if (m_watcom) {
                scanShell(text, m_cwd, out, 0);    // wmake runs `cd` itself: it lasts for the whole recipe
                continue;
            }
            std::stringstream ss(text);
            std::string line;
            while (std::getline(ss, line)) scanShell(line, m_cwd, out, 0);
        }
    }
    return out;
}

void MakeReader::scanFlags(const std::string& text, ContextResult& cr)
{
    const auto toks = shellSplit(m_watcom ? dosSlashes(text) : text);
    auto addInc = [&](const std::string& d) {
        if (d.empty() || d[0] == '-') return;
        const fs::path p = absUnder(m_cwd, d);
        if (!isDirectory(p) || isSystemPath(p)) return;
        if (std::find(cr.include_dirs.begin(), cr.include_dirs.end(), p) == cr.include_dirs.end())
            cr.include_dirs.push_back(p);
    };
    auto addDef = [&](const std::string& d) {
        if (d.empty() || d[0] == '-' || d.find('$') != std::string::npos) return;
        if (std::find(cr.defines.begin(), cr.defines.end(), d) == cr.defines.end())
            cr.defines.push_back(d);
    };
    for (size_t i = 0; i < toks.size(); ++i) {
        const std::string& t = toks[i];
        auto nextTok = [&]() -> std::string { return i + 1 < toks.size() ? toks[++i] : std::string(); };
        if (m_watcom && t.size() >= 2 && (t[0] == '-' || t[0] == '/')) {
            // Watcom:  -i=dir;dir  /i=dir  -dNAME[=val]  /dNAME   (-d0..-d3 / -db are debug switches)
            const char o = (char)std::tolower((unsigned char)t[1]);
            if (o == 'i' && !startsWith(t, "-isystem") && !startsWith(t, "-iquote") && !startsWith(t, "-idirafter")) {
                std::string v = t.substr(2);
                if (v.empty()) v = nextTok();
                else if (v[0] == '=') v.erase(0, 1);
                std::replace(v.begin(), v.end(), ';', ' ');
                for (const auto& d : words(v)) {
                    std::string ds = slashes(d);
                    const fs::path real = findNoCase(absUnder(m_cwd, ds));
                    addInc(real.empty() ? ds : real.string());
                }
                continue;
            }
            if (o == 'd' && t.size() > 2 && (std::isalpha((unsigned char)t[2]) || t[2] == '_') && t.substr(2) != "b") {
                addDef(t.substr(2));
                continue;
            }
        }
        if (t == "-I" || t == "-isystem" || t == "-iquote" || t == "-idirafter") addInc(nextTok());
        else if (startsWith(t, "-isystem"))   addInc(t.substr(8));
        else if (startsWith(t, "-iquote"))    addInc(t.substr(7));
        else if (startsWith(t, "-idirafter")) addInc(t.substr(10));
        else if (startsWith(t, "-I"))         addInc(t.substr(2));
        else if (t == "-D")                   addDef(nextTok());
        else if (startsWith(t, "-D"))         addDef(t.substr(2));
        else if (startsWith(t, "-std=") && cr.cpp_std.empty()) {
            const std::string s = t.substr(5);
            if (startsWith(s, "c++") || startsWith(s, "gnu++")) cr.cpp_std = s;
        }
    }
}

ContextResult MakeReader::analyze()
{
    ContextResult cr;
    cr.cwd = m_cwd;
    cr.watcom = m_watcom;
    buildIndexes();

    //  Merge explicit rules by target, in order of first appearance
    struct Merged {
        std::vector<std::string> prereqs, order_only, recipe;
        std::string stem;
    };
    std::vector<std::string> order;
    std::unordered_map<std::string, Merged> merged;
    for (const Rule& r : m_rules) {
        if (r.pattern) continue;
        for (const auto& t : r.targets) {
            auto [it, fresh] = merged.try_emplace(t);
            if (fresh) order.push_back(t);
            Merged& m = it->second;
            for (const auto& p : r.prereqs)
                if (std::find(m.prereqs.begin(), m.prereqs.end(), p) == m.prereqs.end()) m.prereqs.push_back(p);
            for (const auto& p : r.order_only) m.order_only.push_back(p);
            if (!r.recipe.empty() && m.recipe.empty()) { m.recipe = r.recipe; m.stem = r.stem; }
        }
    }

    //  Reachability from the default goal decides target order
    std::string goal = trim(lookup(".DEFAULT_GOAL"));
    if (goal.empty()) goal = m_default_goal;
    std::unordered_map<std::string, int> rank;
    {
        int next_rank = 0;
        std::vector<std::string> stack;
        if (!goal.empty()) stack.push_back(goal);
        std::unordered_set<std::string> seen;
        while (!stack.empty()) {
            const std::string t = stack.back();
            stack.pop_back();
            if (!seen.insert(t).second) continue;
            rank[t] = next_rank++;
            auto it = merged.find(t);
            if (it == merged.end()) continue;
            for (auto p = it->second.prereqs.rbegin(); p != it->second.prereqs.rend(); ++p) stack.push_back(*p);
        }
    }

    //  Compiler / linker names this makefile uses
    std::set<std::string> tool_words;
    m_analysis = true;
    for (const char* v : {"CC", "CXX", "LD", "AR", "AS", "LINK", "LINKER", "HOSTCC", "HOSTCXX"}) {
        const auto w = words(lookup(v));
        if (!w.empty()) tool_words.insert(w.front());
    }
    m_analysis = false;
    auto isTool = [&](const std::string& tok) { return tool_words.count(tok) || isToolchainCommand(tok); };

    //  Link targets
    std::vector<std::pair<int, FoundTarget>> found;
    int decl = 0;
    for (const auto& t : order) {
        ++decl;
        const Merged& m = merged[t];
        if (t[0] == '.' && t.find('/') == std::string::npos) continue;
        if (m_phony.count(t)) continue;
        if (isObjectFile(t) || isSourceFile(t) || isHeaderFile(t) || isDependencyFile(t)) continue;
        if (t.find('%') != std::string::npos) continue;
        // e.g. $(CROSS_PREFIX)-tcc with the prefix unset: a template, not a target
        if (fs::path(t).filename().string().rfind('-', 0) == 0) continue;

        std::string kind = libraryKind(t);

        bool has_inputs = false;
        for (const auto& p : m.prereqs)
            if (isObjectFile(p) || isSourceFile(p)) { has_inputs = true; break; }

        const std::string recipe = m.recipe.empty() ? "" : expandRecipe(m.recipe, t, m.prereqs, m.order_only, m.stem);

        // Does the recipe run a compiler/linker/archiver?  Remember its -o output.
        bool links = false, shared = false, archive = false;
        std::string out_name;
        std::set<std::string> outputs;   // every distinct -o argument
        std::vector<std::string> recipe_tokens;
        {
            std::stringstream ss(recipe);
            std::string line;
            while (std::getline(ss, line)) {
                std::string l = m_watcom ? dosSlashes(line) : line;
                for (char& c : l) {
                    if (c == ';' || c == '&' || c == '|') c = '\n';
                    else if (m_watcom && (c == '{' || c == '}' || c == ',')) c = ' ';   // wlink file lists
                }
                std::stringstream cs(l);
                std::string cmd;
                while (std::getline(cs, cmd)) {
                    auto toks = shellSplit(cmd);
                    if (toks.empty()) continue;
                    size_t k = 0;
                    while (k < toks.size() && toks[k].find('=') != std::string::npos && toks[k][0] != '-') ++k;
                    if (k >= toks.size() || !isTool(toks[k])) continue;
                    links = true;
                    std::string prog = lower(fs::path(toks[k]).filename().string());
                    if (endsWith(prog, ".exe")) prog.resize(prog.size() - 4);
                    if (prog == "ar" || endsWith(prog, "-ar") || prog == "llvm-ar" || prog == "lib" ||
                        prog == "wlib" || prog == "tlib")
                        archive = true;
                    const bool wlink = prog == "wlink";
                    for (size_t j = k + 1; j < toks.size(); ++j) {
                        const std::string& a = toks[j];
                        const std::string la = lower(a);
                        if (a == "-shared" || a == "-dynamiclib" || a == "/DLL") shared = true;
                        if (m_watcom && (la == "-bd" || la == "/bd")) shared = true;
                        if (wlink && j > k + 1 && (lower(toks[j - 1]) == "system" || lower(toks[j - 1]) == "sys") &&
                            la.find("dll") != std::string::npos)
                            shared = true;
                        // wlink ... Name out.exe   /   wcl -fe=out.exe
                        std::string wout;
                        if (wlink && (la == "name" || la == "n") && j + 1 < toks.size()) wout = toks[j + 1];
                        else if (m_watcom && (startsWith(la, "-fe=") || startsWith(la, "/fe="))) wout = a.substr(4);
                        if (!wout.empty()) {
                            out_name = wout;
                            outputs.insert(wout);
                        }
                        if (a == "-o" && j + 1 < toks.size()) out_name = toks[j + 1];
                        else if (startsWith(a, "-o") && a.size() > 2 && a[2] != '-') out_name = a.substr(2);
                        if ((a == "-o" || (startsWith(a, "-o") && a.size() > 2 && a[2] != '-')) &&
                            !isObjectFile(out_name))
                            outputs.insert(out_name);
                        recipe_tokens.push_back(a);
                    }
                }
            }
        }
        for (const auto& a : recipe_tokens)
            if (isObjectFile(a) || isSourceFile(a)) has_inputs = true;

        // A recipe that writes several different programs (a test driver,
        // say) isn't the link step of this target.
        if (outputs.size() > 1 && !outputs.count(t)) continue;

        if (!kind.empty() && !has_inputs && !links) continue;   // e.g. an install rule copying a library
        if (kind.empty()) {
            if (!has_inputs) continue;
            if (m.recipe.empty()) {
                // GNU make's implicit link rule:  prog: prog.o util.o
                bool implicit = false;
                for (const auto& p : m.prereqs) {
                    const std::string b = p.substr(0, p.size() - extOf(p).size());
                    if (b == t && (isObjectFile(p) || isSourceFile(p))) implicit = true;
                }
                if (!implicit) continue;
            } else if (!links) {
                continue;
            }
            kind = archive ? "static_library" : shared ? "shared_library" : "executable";
        }

        FoundTarget ft;
        std::string output = t;
        if (!out_name.empty() && out_name != t && !isObjectFile(out_name) &&
            merged.find(out_name) == merged.end() && out_name.find('$') == std::string::npos &&
            fs::path(t).stem().string() != out_name)       // wlink "Name cloud" builds cloud.exe
            output = out_name;   // e.g.  all: $(OBJS) ; $(CC) -o prog $^
        ft.output = absUnder(m_cwd, output);
        ft.type = kind;

        auto takeInput = [&](const std::string& p) {
            if (isSourceFile(p) || isHeaderFile(p)) {
                if (auto r = resolve(p); !r.empty() && !isSystemPath(r)) ft.add(r);
            } else if (isObjectFile(p)) {
                for (const auto& r : objectInputs(p)) ft.add(r);
            } else if (!libraryKind(p).empty()) {
                ft.links.push_back(absUnder(m_cwd, p).generic_string());
            }
        };
        for (const auto& p : m.prereqs) takeInput(p);
        for (const auto& a : recipe_tokens) {
            if (startsWith(a, "-l") && a.size() > 2) ft.links.push_back(a);
            else if (isSourceFile(a) || isObjectFile(a)) takeInput(a);
            else if (!libraryKind(a).empty()) takeInput(a);
        }

        const int r = rank.count(t) ? rank[t] : 1000000 + decl;
        found.emplace_back(r, std::move(ft));
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& f : found) cr.targets.push_back(std::move(f.second));

    //  Flags: every variable and every compile recipe
    m_analysis = true;
    std::vector<std::pair<std::string, Var>> vars(m_vars.begin(), m_vars.end());
    std::sort(vars.begin(), vars.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [name, v] : vars) {
        if (v.origin == "default" || v.origin == "environment") continue;
        if (name == "MAKEFILE_LIST") continue;
        const std::string val = v.recursive ? expand(v.value) : v.value;
        scanFlags(val, cr);
        for (const auto& w : words(val)) {
            if (!isSourceFile(w) && !isHeaderFile(w)) continue;
            if (auto r = resolve(w); !r.empty() && !isSystemPath(r) &&
                std::find(cr.loose.begin(), cr.loose.end(), r) == cr.loose.end())
                cr.loose.push_back(r);
        }
    }
    m_analysis = false;
    for (const Rule& r : m_rules) {
        if (r.recipe.empty()) continue;
        const std::string& t = r.targets.front();
        if (!(r.pattern ? isObjectFile(t) || t == "%" : isObjectFile(t))) continue;
        scanFlags(expandRecipe(r.recipe, t, r.prereqs, r.order_only, r.stem), cr);
    }

    // Prerequisites of any rule that name project files (e.g. `tags: $(SRCS)`)
    for (const Rule& r : m_rules) {
        if (r.pattern) continue;
        for (const auto& p : r.prereqs) {
            if (!isSourceFile(p) && !isHeaderFile(p)) continue;
            if (auto f = resolve(p); !f.empty() && !isSystemPath(f) &&
                std::find(cr.loose.begin(), cr.loose.end(), f) == cr.loose.end())
                cr.loose.push_back(f);
        }
    }
    // wmake `.h : dir;dir` search paths double as include directories
    for (const auto& d : headerSearchDirs()) {
        const fs::path real = findNoCase(d);
        if (!real.empty() && isDirectory(real) && !isSystemPath(real) &&
            std::find(cr.include_dirs.begin(), cr.include_dirs.end(), real) == cr.include_dirs.end())
            cr.include_dirs.push_back(real);
    }
    return cr;
}

//  #include scanning

struct IncludeScanner {
    std::unordered_map<std::string, std::vector<std::pair<bool, std::string>>> cache;

    const std::vector<std::pair<bool, std::string>>& includesOf(const fs::path& file)
    {
        const std::string key = file.generic_string();
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        auto& out = cache[key];

        std::error_code ec;
        const auto sz = fs::file_size(file, ec);
        if (ec || sz > (4u << 20)) return out;
        std::ifstream f(file, std::ios::binary);
        std::string line;
        while (std::getline(f, line)) {
            size_t i = 0;
            while (i < line.size() && isBlank(line[i])) ++i;
            if (i >= line.size() || line[i] != '#') continue;
            ++i;
            while (i < line.size() && isBlank(line[i])) ++i;
            size_t kw = 0;
            if (line.compare(i, 7, "include") == 0) kw = 7;
            else if (line.compare(i, 6, "import") == 0) kw = 6;
            else continue;
            i += kw;
            while (i < line.size() && isBlank(line[i])) ++i;
            if (i >= line.size()) continue;
            const char open = line[i];
            const char close = open == '"' ? '"' : open == '<' ? '>' : 0;
            if (!close) continue;
            const size_t e = line.find(close, i + 1);
            if (e == std::string::npos) continue;
            out.emplace_back(open == '<', slashes(line.substr(i + 1, e - i - 1)));
        }
        return out;
    }

    // Add every project header reachable from `t`'s files.
    void expand(FoundTarget& t, const std::vector<fs::path>& inc_dirs, bool no_case = false)
    {
        std::deque<fs::path> queue(t.files.begin(), t.files.end());
        std::unordered_set<std::string> visited;
        size_t budget = 5000;
        while (!queue.empty() && budget > 0) {
            const fs::path f = queue.front();
            queue.pop_front();
            if (!visited.insert(f.generic_string()).second) continue;
            --budget;
            for (const auto& [angled, name] : includesOf(f)) {
                // Case-insensitive fallback for DOS-era sources (#include "TCP.H" → tcp.h)
                auto tryFile = [&](const fs::path& q) -> fs::path {
                    if (isRegularFile(q)) return q;
                    if (no_case)
                        if (fs::path r = findNoCase(q); !r.empty() && isRegularFile(r)) return r;
                    return {};
                };
                fs::path hit;
                if (!angled) hit = tryFile((f.parent_path() / name).lexically_normal());
                if (hit.empty()) {
                    for (const auto& d : inc_dirs)
                        if (hit = tryFile((d / name).lexically_normal()); !hit.empty()) break;
                }
                if (hit.empty() || isSystemPath(hit)) continue;
                t.add(hit);
                queue.push_back(hit);
            }
        }
    }
};

// A makefile read with different command-line variables is a different
// invocation (e.g. `$(MAKE) -f module.mk MODULE=net` for several modules).
std::string pendingKey(const fs::path& file, std::vector<std::pair<std::string, std::string>> vars)
{
    std::sort(vars.begin(), vars.end());
    vars.erase(std::unique(vars.begin(), vars.end()), vars.end());
    std::string k = file.generic_string();
    for (const auto& [n, v] : vars) k += '\x01' + n + '=' + v;
    return k;
}

std::string relTo(const fs::path& p, const fs::path& root)
{
    fs::path r = p.lexically_relative(root);
    if (r.empty()) return p.generic_string();
    return r.generic_string();
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════

bool MakefileImporter::looksLikeMakefile(const std::string& path)
{
    const std::string fn = fs::path(path).filename().string();
    std::string low = fn;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (low == "makefile" || low == "gnumakefile" || startsWith(low, "makefile.")) return true;
    const std::string e = extOf(low);
    return e == ".mk" || e == ".mak" || e == ".make" || e == ".mif" || e == ".wmk" || e == ".wat";
}

MakefileImportResult MakefileImporter::import(const std::string& makefile_path)
{
    return import(makefile_path, Options{});
}

MakefileImportResult MakefileImporter::import(const std::string& makefile_path, const Options& opts)
{
    MakefileImportResult res;

    std::error_code ec;
    const fs::path top = fs::absolute(fs::path(makefile_path), ec).lexically_normal();
    if (ec || !isRegularFile(top)) {
        res.error = "Makefile not found: " + makefile_path;
        return res;
    }
    const fs::path root = top.parent_path();

    SharedState st;
    st.opts = opts;

    //  Read the top makefile and every sub-make it reaches
    struct Pending {
        fs::path dir, file;
        std::vector<std::pair<std::string, std::string>> cmdline, env;
        bool watcom = false;
    };
    noCaseDirs().clear();
    std::deque<Pending> queue;
    queue.push_back({root, top, {}, {}});
    std::set<std::string> visited;
    std::vector<std::unique_ptr<MakeReader>> readers;

    while (!queue.empty() && (int)readers.size() < opts.max_makefiles) {
        Pending pd = std::move(queue.front());
        queue.pop_front();
        if (!visited.insert(pendingKey(pd.file, pd.cmdline)).second) continue;

        auto rd = std::make_unique<MakeReader>(st, pd.dir);
        rd->setWatcom(pd.watcom || sniffWatcom(pd.file));
        for (const auto& [n, v] : pd.env) rd->setEnvironmentVar(n, v);
        for (const auto& [n, v] : pd.cmdline) rd->setCommandLineVar(n, v);
        rd->top_makefile = pd.file;
        if (!rd->readMakefile(pd.file, true)) {
            if (readers.empty()) {
                res.error = "Cannot read " + pd.file.string();
                return res;
            }
            continue;
        }

        const auto exported = rd->exportedVars();
        for (auto& sm : rd->findSubMakes()) {
            fs::path file;
            if (!sm.file.empty()) {
                file = absUnder(sm.dir, sm.file);
                if (!isRegularFile(file))
                    if (fs::path q = findNoCase(file); !q.empty()) file = q;
            } else {
                for (const char* n : {"GNUmakefile", "makefile", "Makefile"}) {
                    if (isRegularFile(sm.dir / n)) { file = (sm.dir / n).lexically_normal(); break; }
                }
                if (file.empty() && sm.watcom)                   // MAKEFILE on a DOS tree
                    if (fs::path q = findNoCase(sm.dir / "makefile"); !q.empty()) file = q;
                if (file.empty()) {
                    st.warn("No makefile found in " + sm.dir.string());
                    continue;
                }
            }
            if (file == pd.file && sm.vars.empty()) continue;   // `$(MAKE) clean` style self-recursion
            Pending np;
            np.dir = sm.dir;
            np.file = file;
            np.cmdline = rd->commandLineVars();
            for (auto& v : sm.vars) np.cmdline.push_back(v);
            if (visited.count(pendingKey(np.file, np.cmdline))) continue;
            np.env = exported;
            np.watcom = sm.watcom;
            queue.push_back(std::move(np));
        }
        readers.push_back(std::move(rd));
    }
    if (!queue.empty())
        st.warn("Stopped after " + std::to_string(opts.max_makefiles) + " makefiles");

    //  Analyse each make invocation
    std::vector<ContextResult> results;
    for (auto& rd : readers) results.push_back(rd->analyze());

    //  Assemble the project
    GediProject& proj = res.project;
    proj.root         = root.string();
    proj.name         = root.filename().string();
    if (proj.name.empty()) proj.name = "project";
    proj.build_system = "make";
    proj.build_file   = relTo(top, root);
    if (readers.front()->watcom()) proj.make_tool = "wmake";

    std::set<std::string> owned;
    std::map<std::string, std::string> output_to_name;   // abs output → target name
    std::vector<FoundTarget> all;
    std::vector<size_t> all_ctx;
    IncludeScanner scanner;

    for (size_t ci = 0; ci < results.size(); ++ci) {
        ContextResult& cr = results[ci];
        for (auto& ft : cr.targets) {
            const std::string key = ft.output.generic_string();
            if (output_to_name.count(key)) continue;       // same output built in two places
            output_to_name[key] = relTo(ft.output, root);
            scanner.expand(ft, cr.include_dirs, cr.watcom);
            all.push_back(std::move(ft));
            all_ctx.push_back(ci);
        }
    }
    for (const auto& ft : all)
        for (const auto& f : ft.files) owned.insert(f.generic_string());

    // Files the makefiles mention that no target claimed
    for (size_t ci = 0; ci < results.size(); ++ci) {
        ContextResult& cr = results[ci];
        FoundTarget extra;
        for (const auto& f : cr.loose)
            if (!owned.count(f.generic_string())) extra.add(f);
        if (extra.files.empty()) continue;
        scanner.expand(extra, cr.include_dirs, cr.watcom);

        std::vector<fs::path> fresh;
        for (const auto& f : extra.files)
            if (owned.insert(f.generic_string()).second) fresh.push_back(f);
        if (fresh.empty()) continue;

        // One target in this directory?  The files belong to it.
        std::vector<size_t> mine;
        for (size_t k = 0; k < all.size(); ++k)
            if (all_ctx[k] == ci) mine.push_back(k);
        if (mine.size() == 1) {
            for (const auto& f : fresh) all[mine[0]].add(f);
            continue;
        }

        FoundTarget grp;
        std::string label = relTo(cr.cwd, root);
        if (label == "." || label.empty()) label = proj.name;
        grp.output = cr.cwd / (mine.empty() ? label : label + " (other files)");
        grp.type = "executable";
        for (const auto& f : fresh) grp.add(f);
        output_to_name[grp.output.generic_string()] = mine.empty() ? label : label + " (other files)";
        all.push_back(std::move(grp));
        all_ctx.push_back(ci);
    }

    // Targets → ProjectTargets, resolving link dependencies between them
    std::set<std::string> distinct;
    for (const auto& ft : all) {
        ProjectTarget pt;
        pt.name = output_to_name[ft.output.generic_string()];
        pt.type = ft.type;
        std::vector<fs::path> files = ft.files;           // sources first, then headers
        std::stable_partition(files.begin(), files.end(),
                              [](const fs::path& f) { return isSourceFile(f.string()); });
        for (const auto& f : files) {
            pt.sources.push_back(relTo(f, root));
            distinct.insert(f.generic_string());
        }
        for (const auto& l : ft.links) {
            std::string dep;
            if (auto it = output_to_name.find(l); it != output_to_name.end()) {
                dep = it->second;
            } else if (startsWith(l, "-l")) {
                const std::string n = l.substr(2);
                for (const auto& [out, name] : output_to_name) {
                    const std::string fn = fs::path(out).filename().string();
                    if (fn == "lib" + n + ".a" || fn == "lib" + n + ".so" || fn == n + ".lib" ||
                        fn == "lib" + n + ".dylib" || startsWith(fn, "lib" + n + ".so.")) {
                        dep = name;
                        break;
                    }
                }
            }
            if (!dep.empty() && dep != pt.name &&
                std::find(pt.link_targets.begin(), pt.link_targets.end(), dep) == pt.link_targets.end())
                pt.link_targets.push_back(dep);
        }
        proj.targets.push_back(std::move(pt));
    }
    res.source_count = distinct.size();

    // Include directories, defines and language standard (for code intelligence)
    for (const auto& cr : results) {
        for (const auto& d : cr.include_dirs) {
            const std::string rel = relTo(d, root);
            if (std::find(proj.include_dirs.begin(), proj.include_dirs.end(), rel) == proj.include_dirs.end())
                proj.include_dirs.push_back(rel);
        }
        for (const auto& d : cr.defines)
            if (std::find(proj.defines.begin(), proj.defines.end(), d) == proj.defines.end())
                proj.defines.push_back(d);
        if (!cr.cpp_std.empty() && proj.compiler_settings.cpp_standard == CompilerSettings{}.cpp_standard) {
            std::string s = cr.cpp_std;
            if (startsWith(s, "gnu++")) s = "c++" + s.substr(5);
            static const std::map<std::string, std::string> aliases = {
                {"c++0x", "c++11"}, {"c++1y", "c++14"}, {"c++1z", "c++17"},
                {"c++2a", "c++20"}, {"c++2b", "c++23"}, {"c++2c", "c++26"},
            };
            if (auto a = aliases.find(s); a != aliases.end()) s = a->second;
            proj.compiler_settings.cpp_standard = s;
            proj.cpp_standard = s;
        }
    }

    for (const auto& m : st.makefiles) {
        if (m == top || isDependencyFile(m.string())) continue;
        proj.aux_build_files.push_back(relTo(m, root));
    }

    res.makefiles.clear();
    for (const auto& m : st.makefiles) res.makefiles.push_back(m.string());
    res.warnings = std::move(st.warnings);

    if (proj.targets.empty())
        res.warnings.push_back("No build targets or source files were recognised.");

    res.ok = true;
    return res;
}
