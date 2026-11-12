#pragma once
// Searching across files: the pieces Find in Files and Quick Open share.
#include <cstddef>
#include <filesystem>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace filesearch {

// The files under `root`, as paths relative to it ('/' separated), sorted.
// Skips hidden directories (.git, .cache ...) and build output directories
// (build, builddir, cmake-build-*, node_modules); stops after `cap` files.
std::vector<std::string> listFiles(const std::filesystem::path& root, std::size_t cap = 50000);

// Whether file contents look binary (a NUL byte in the first 8000 bytes).
bool looksBinary(const std::string& data);

// What to look for, and how.
struct Query {
    std::string text;
    bool match_case = false;
    bool whole_word = false;
    bool regex      = false;
};

class Matcher {
public:
    explicit Matcher(const Query& q);
    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }   // a bad regular expression
    // Every match in `line`, as (byte offset, length), left to right.
    void find(const std::string& line, std::vector<std::pair<std::size_t, std::size_t>>& out) const;

private:
    Query       q_;
    std::string needle_;   // plain search: the text, lower-cased unless match_case
    std::regex  re_;
    std::string error_;
};

// Fuzzy match of `pattern` (spaces ignored, case-insensitive) against a
// relative path: -1 when its characters do not all appear in order, else a
// score - higher is better. Matches in the file name, at word starts and in
// runs score higher. `positions` (optional) receives the matched byte offsets.
int fuzzyScore(const std::string& pattern, const std::string& path, std::vector<int>* positions = nullptr);

} // namespace filesearch
