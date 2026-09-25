#include "Yaml.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace yaml {

using nlohmann::json;

namespace {

struct ParseError : std::runtime_error {
    ParseError(int line, const std::string& msg)
        : std::runtime_error("line " + std::to_string(line + 1) + ": " + msg) {}
};

std::string rtrim(const std::string& s) {
    size_t e = s.find_last_not_of(" \t\r");
    return (e == std::string::npos) ? "" : s.substr(0, e + 1);
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

// Strip an unquoted trailing "# comment". A '#' only starts a comment when it is
// at the start of the (trimmed) text or preceded by whitespace - so a value like
// "#RRGGBB" or a path fragment survives.
std::string stripComment(const std::string& s) {
    bool in_sq = false, in_dq = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\'' && !in_dq)                      in_sq = !in_sq;
        else if (c == '"' && !in_sq)                  in_dq = !in_dq;
        else if (c == '#' && !in_sq && !in_dq && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t'))
            return s.substr(0, i);
    }
    return s;
}

// A scalar in its final typed form.
json typedScalar(const std::string& raw) {
    std::string v = trim(raw);
    if (v.empty()) return std::string();

    // Quoted: always a string.
    if (v.size() >= 2 && v.front() == '\'' && v.back() == '\'')
        return v.substr(1, v.size() - 2);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        std::string out;
        for (size_t i = 1; i + 1 < v.size(); ++i) {
            if (v[i] == '\\' && i + 2 < v.size()) {
                char n = v[++i];
                switch (n) {
                    case 'n':  out += '\n'; break;
                    case 't':  out += '\t'; break;
                    case 'r':  out += '\r'; break;
                    case '0':  out += '\0'; break;
                    default:   out += n;    break;   // covers \\ and \"
                }
            } else {
                out += v[i];
            }
        }
        return out;
    }

    if (v == "true"  || v == "yes" || v == "on")  return true;
    if (v == "false" || v == "no"  || v == "off") return false;
    if (v == "null"  || v == "~")                 return nullptr;

    // Integer, if the whole token is one.
    char* end = nullptr;
    long long n = std::strtoll(v.c_str(), &end, 10);
    if (end && *end == '\0' && end != v.c_str()) return n;

    return v;
}

// Split a flow sequence "[a, b, c]" on top-level commas.
json flowSequence(const std::string& body) {
    json arr = json::array();
    std::string cur;
    bool in_sq = false, in_dq = false;
    int depth = 0;
    for (char c : body) {
        if (c == '\'' && !in_dq)      in_sq = !in_sq;
        else if (c == '"' && !in_sq)  in_dq = !in_dq;
        if (!in_sq && !in_dq) {
            if (c == '[' || c == '{') ++depth;
            if (c == ']' || c == '}') --depth;
            if (c == ',' && depth == 0) { arr.push_back(typedScalar(cur)); cur.clear(); continue; }
        }
        cur += c;
    }
    if (!trim(cur).empty()) arr.push_back(typedScalar(cur));
    return arr;
}

// Index of the ':' that separates a mapping key from its value, or npos.
size_t keySeparator(const std::string& s) {
    bool in_sq = false, in_dq = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\'' && !in_dq)      in_sq = !in_sq;
        else if (c == '"' && !in_sq)  in_dq = !in_dq;
        else if (c == ':' && !in_sq && !in_dq) {
            // A key separator is ':' at end of line or followed by whitespace.
            // That keeps "C:\path" and "http://x" usable as unquoted values.
            if (i + 1 == s.size() || s[i + 1] == ' ' || s[i + 1] == '\t') return i;
        }
    }
    return std::string::npos;
}

class Parser {
public:
    explicit Parser(const std::string& text) {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines_.push_back(line);
        }
    }

    json parseDocument() {
        skipBlanks();
        if (pos_ >= lines_.size()) return json::object();
        return parseNode(indentOf(pos_));
    }

private:
    std::vector<std::string> lines_;
    size_t pos_ = 0;

    bool isBlank(size_t i) const {
        const std::string t = trim(lines_[i]);
        return t.empty() || t[0] == '#';
    }

    void skipBlanks() {
        while (pos_ < lines_.size() && isBlank(pos_)) ++pos_;
    }

    int indentOf(size_t i) const {
        int n = 0;
        while (n < (int)lines_[i].size() && lines_[i][n] == ' ') ++n;
        return n;
    }

    // Body of a line: comment stripped, indentation removed.
    std::string bodyOf(size_t i) const { return rtrim(stripComment(lines_[i]).substr(indentOf(i))); }

    void checkTabs(size_t i) const {
        const std::string& l = lines_[i];
        for (size_t k = 0; k < l.size() && (l[k] == ' ' || l[k] == '\t'); ++k)
            if (l[k] == '\t') throw ParseError((int)i, "tab used for indentation");
    }

    // A block mapping or block sequence whose items sit at column `indent`.
    json parseNode(int indent) {
        skipBlanks();
        if (pos_ >= lines_.size()) return nullptr;
        checkTabs(pos_);
        const std::string body = bodyOf(pos_);
        if (body == "-" || body.rfind("- ", 0) == 0) return parseSequence(indent);
        return parseMapping(indent);
    }

    json parseMapping(int indent) {
        json obj = json::object();
        while (true) {
            skipBlanks();
            if (pos_ >= lines_.size()) break;
            checkTabs(pos_);
            const int ind = indentOf(pos_);
            if (ind < indent) break;
            if (ind > indent) throw ParseError((int)pos_, "unexpected indentation in mapping");

            const std::string body = bodyOf(pos_);
            if (body.rfind("- ", 0) == 0 || body == "-")
                throw ParseError((int)pos_, "sequence item where a mapping key was expected");

            const size_t sep = keySeparator(body);
            if (sep == std::string::npos)
                throw ParseError((int)pos_, "expected 'key: value'");

            const std::string key = trim(typedScalarKey(body.substr(0, sep)));
            const std::string val = trim(body.substr(sep + 1));
            const size_t key_line = pos_++;

            obj[key] = parseValue(val, indent, key_line);
        }
        return obj;
    }

    // Keys are always strings, quoted or not.
    static std::string typedScalarKey(const std::string& raw) {
        std::string k = trim(raw);
        if (k.size() >= 2 && ((k.front() == '"' && k.back() == '"') ||
                              (k.front() == '\'' && k.back() == '\'')))
            return k.substr(1, k.size() - 2);
        return k;
    }

    json parseSequence(int indent) {
        json arr = json::array();
        while (true) {
            skipBlanks();
            if (pos_ >= lines_.size()) break;
            checkTabs(pos_);
            const int ind = indentOf(pos_);
            if (ind < indent) break;
            if (ind > indent) throw ParseError((int)pos_, "unexpected indentation in sequence");

            const std::string body = bodyOf(pos_);
            if (body != "-" && body.rfind("- ", 0) != 0) break;

            const std::string rest = (body == "-") ? "" : trim(body.substr(2));
            if (rest.empty()) {
                // Item content lives on the following, more-indented lines.
                ++pos_;
                skipBlanks();
                if (pos_ < lines_.size() && indentOf(pos_) > indent) arr.push_back(parseNode(indentOf(pos_)));
                else                                                 arr.push_back(nullptr);
                continue;
            }

            if (keySeparator(rest) != std::string::npos) {
                // "- key: value": a mapping whose first key starts right after the
                // dash. Rewrite the dash as spaces so the mapping parser sees a
                // normal block at that column, then let it consume the rest.
                const int item_indent = indent + 2;
                lines_[pos_] = std::string(item_indent, ' ') + rest;
                arr.push_back(parseMapping(item_indent));
                continue;
            }

            ++pos_;
            arr.push_back(typedScalar(rest));
        }
        return arr;
    }

    // The value part of "key: <val>", which may continue on following lines.
    json parseValue(const std::string& val, int indent, size_t key_line) {
        if (!val.empty() && (val[0] == '|' || val[0] == '>'))
            return blockScalar(val, indent);

        if (!val.empty() && val.front() == '[') {
            const size_t close = val.rfind(']');
            if (close == std::string::npos) throw ParseError((int)key_line, "unterminated flow sequence");
            return flowSequence(val.substr(1, close - 1));
        }

        if (!val.empty()) return typedScalar(val);

        // Empty value: a nested block, or null when nothing more-indented follows.
        skipBlanks();
        if (pos_ < lines_.size() && indentOf(pos_) > indent) return parseNode(indentOf(pos_));
        return nullptr;
    }

    // "|", "|-", ">", ">-" followed by more-indented lines.
    json blockScalar(const std::string& header, int indent) {
        const bool folded = (header[0] == '>');
        const bool strip  = header.find('-') != std::string::npos;

        // The block's own indentation is that of its first non-blank line.
        size_t first = pos_;
        while (first < lines_.size() && trim(lines_[first]).empty()) ++first;
        if (first >= lines_.size() || indentOf(first) <= indent) return std::string();
        const int base = indentOf(first);

        std::vector<std::string> out;
        while (pos_ < lines_.size()) {
            const std::string& l = lines_[pos_];
            if (trim(l).empty()) { out.push_back(""); ++pos_; continue; }
            if (indentOf(pos_) < base) break;
            out.push_back(rtrim(l.substr(base)));   // comments are content here
            ++pos_;
        }
        while (!out.empty() && out.back().empty()) out.pop_back();

        std::string text;
        for (size_t i = 0; i < out.size(); ++i) {
            text += out[i];
            if (i + 1 < out.size()) {
                // Folded mode joins with a space, except that a blank line (a
                // paragraph break) and an indented "more literal" line keep their
                // newline - the same rule real YAML applies.
                const bool keep_nl = !folded || out[i].empty() || out[i + 1].empty() ||
                                     (!out[i + 1].empty() && out[i + 1][0] == ' ');
                text += keep_nl ? "\n" : " ";
            }
        }
        if (!strip && !text.empty()) text += "\n";
        return text;
    }
};

} // namespace

json parse(const std::string& text, std::string& error) {
    try {
        error.clear();
        Parser p(text);
        return p.parseDocument();
    } catch (const std::exception& e) {
        error = e.what();
        return json(json::value_t::discarded);
    }
}

json parseFile(const std::string& path, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open " + path;
        return json(json::value_t::discarded);
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return parse(ss.str(), error);
}

} // namespace yaml
