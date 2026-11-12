#include "FileSearch.h"

#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace filesearch {

static bool skippedDirectory(const std::string& name)
{
    if (name.empty() || name[0] == '.') return true;                 // .git, .cache, .vscode ...
    if (name == "build" || name == "builddir" || name == "node_modules" ||
        name == "__pycache__")
        return true;
    return name.rfind("cmake-build-", 0) == 0;
}

std::vector<std::string> listFiles(const fs::path& root, std::size_t cap)
{
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& e = *it;
        const std::string name = e.path().filename().string();
        std::error_code tec;
        if (e.is_directory(tec)) {
            // Symlinked directories are not followed (no loops), skipped ones not entered.
            if (skippedDirectory(name) || e.is_symlink(tec)) it.disable_recursion_pending();
            continue;
        }
        if (!e.is_regular_file(tec)) continue;
        out.push_back(e.path().lexically_relative(root).generic_string());
        if (out.size() >= cap) break;
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool looksBinary(const std::string& data)
{
    const std::size_t n = std::min<std::size_t>(data.size(), 8000);
    return data.find('\0') < n;
}

//  Matcher

static bool wordChar(unsigned char c) { return std::isalnum(c) || c == '_'; }

static std::string lowered(const std::string& s)
{
    std::string r = s;
    for (auto& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

Matcher::Matcher(const Query& q) : q_(q)
{
    if (q_.text.empty()) { error_ = "Nothing to search for."; return; }
    if (q_.regex) {
        std::string pat = q_.whole_word ? "\\b(?:" + q_.text + ")\\b" : q_.text;
        auto flags = std::regex::ECMAScript | std::regex::optimize;
        if (!q_.match_case) flags |= std::regex::icase;
        try {
            re_ = std::regex(pat, flags);
        } catch (const std::regex_error& e) {
            error_ = std::string("Invalid regular expression: ") + e.what();
        }
    } else {
        needle_ = q_.match_case ? q_.text : lowered(q_.text);
    }
}

void Matcher::find(const std::string& line, std::vector<std::pair<std::size_t, std::size_t>>& out) const
{
    out.clear();
    if (!ok()) return;

    if (q_.regex) {
        for (auto it = std::sregex_iterator(line.begin(), line.end(), re_);
             it != std::sregex_iterator(); ++it) {
            if (it->length(0) == 0) continue;   // empty matches ("x*") mark nothing
            out.emplace_back((std::size_t)it->position(0), (std::size_t)it->length(0));
        }
        return;
    }

    const std::string hay = q_.match_case ? line : lowered(line);
    std::size_t pos = 0;
    while ((pos = hay.find(needle_, pos)) != std::string::npos) {
        const std::size_t end = pos + needle_.size();
        const bool word_ok = !q_.whole_word ||
            ((pos == 0 || !wordChar((unsigned char)hay[pos - 1])) &&
             (end >= hay.size() || !wordChar((unsigned char)hay[end])));
        if (word_ok) { out.emplace_back(pos, needle_.size()); pos = end; }
        else         { ++pos; }
    }
}

//  Fuzzy scoring

// Matches `pat` in `text` from `start` on, each character as early as possible.
static bool greedy(const std::string& pat, const std::string& text, std::size_t start, std::vector<int>& pos)
{
    pos.clear();
    std::size_t t = start;
    for (char c : pat) {
        while (t < text.size() && text[t] != c) ++t;
        if (t >= text.size()) return false;
        pos.push_back((int)t++);
    }
    return true;
}

static int scorePositions(const std::string& text, const std::vector<int>& pos, int base_start)
{
    int score = 0;
    for (std::size_t i = 0; i < pos.size(); ++i) {
        const int p = pos[i];
        score += 1;
        if (i > 0 && pos[i - 1] == p - 1) score += 5;                  // a run
        if (p == 0 || std::string("/_-. ").find(text[p - 1]) != std::string::npos)
            score += 8;                                                 // a word start
        if (p >= base_start) score += 2;                                // in the file name
    }
    if (!pos.empty() && pos.front() >= base_start) score += 20;         // all in the file name
    return score;
}

int fuzzyScore(const std::string& pattern, const std::string& path, std::vector<int>* positions)
{
    std::string pat;
    for (char c : pattern)
        if (c != ' ') pat += (char)std::tolower((unsigned char)c);
    if (pat.empty()) { if (positions) positions->clear(); return 0; }

    const std::string text = lowered(path);
    const std::size_t slash = text.rfind('/');
    const int base_start = slash == std::string::npos ? 0 : (int)slash + 1;

    // Try each place the first character occurs; keep the best-scoring match.
    int best = -1;
    std::vector<int> cur, best_pos;
    for (std::size_t s = text.find(pat[0]); s != std::string::npos; s = text.find(pat[0], s + 1)) {
        if (!greedy(pat, text, s, cur)) break;          // no later start can match either
        const int sc = scorePositions(text, cur, base_start);
        if (sc > best) { best = sc; best_pos = cur; }
    }
    if (best < 0) return -1;

    const std::string base = text.substr(base_start);
    if (base.rfind(pat, 0) == 0) best += 30;                            // the name starts with it
    const std::size_t dot = base.rfind('.');
    if (base.substr(0, dot) == pat) best += 40;                         // ... or is it
    best -= (int)text.size() / 8;                                       // shorter paths first
    if (positions) *positions = best_pos;
    return best;
}

} // namespace filesearch
