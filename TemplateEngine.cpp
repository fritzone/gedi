#include "TemplateEngine.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

namespace tmpl {

using nlohmann::json;

namespace {

// ── AST ─────────────────────────────────────────────────────────────────────
struct Node {
    enum Kind { TEXT, VAR, FOR, IF } kind = TEXT;
    std::string a;             // TEXT: literal; VAR: path; FOR: loop var; IF: condition
    std::string b;             // FOR: iterable path
    std::vector<Node> body;
    std::vector<Node> elseBody;
};

std::string trim(const std::string& x) {
    size_t a = x.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = x.find_last_not_of(" \t\r\n");
    return x.substr(a, b - a + 1);
}

std::vector<std::string> splitPath(const std::string& p) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : p) {
        if (c == '.') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ── Parser ──────────────────────────────────────────────────────────────────
struct Parser {
    const std::string& s;
    size_t i = 0;
    explicit Parser(const std::string& src) : s(src) {}

    // Parse nodes into `out` until an {% ... %} whose keyword is in `ends` (or EOF).
    // Returns the terminating keyword ("" at EOF).
    std::string parse(std::vector<Node>& out, const std::vector<std::string>& ends) {
        std::string text;
        auto flush = [&]() {
            if (!text.empty()) { Node n; n.kind = Node::TEXT; n.a = text; out.push_back(std::move(n)); text.clear(); }
        };

        while (i < s.size()) {
            if (s[i] == '{' && i + 1 < s.size() && (s[i + 1] == '{' || s[i + 1] == '%')) {
                char kind = s[i + 1];
                const std::string close = (kind == '%') ? "%}" : "}}";
                size_t end = s.find(close, i + 2);
                if (end == std::string::npos) { text += s.substr(i); i = s.size(); break; }

                // A block tag is "line-leading" when only whitespace precedes it on
                // its line. Such a tag has its whole line removed (lstrip + trim);
                // an inline tag ({% for %} mid-line) leaves surrounding text alone.
                bool line_leading = false;
                if (kind == '%') {
                    size_t nl = text.find_last_of('\n');
                    size_t from = (nl == std::string::npos) ? 0 : nl + 1;
                    line_leading = (text.find_first_not_of(" \t", from) == std::string::npos);
                    if (line_leading) text.erase(from);      // lstrip_blocks
                }

                std::string inner = trim(s.substr(i + 2, end - (i + 2)));
                i = end + 2;

                if (kind == '{') {                    // {{ variable }}
                    flush();
                    Node n; n.kind = Node::VAR; n.a = inner; out.push_back(std::move(n));
                    continue;
                }

                // trim_blocks: swallow one trailing newline, but only for a tag that
                // stood alone on its line.
                if (line_leading) {
                    if (i < s.size() && s[i] == '\r') ++i;
                    if (i < s.size() && s[i] == '\n') ++i;
                }

                std::istringstream iss(inner);
                std::string kw; iss >> kw;

                if (std::find(ends.begin(), ends.end(), kw) != ends.end()) { flush(); return kw; }

                if (kw == "for") {
                    std::string var, in, path; iss >> var >> in >> path;   // "for VAR in PATH"
                    flush();
                    Node n; n.kind = Node::FOR; n.a = var; n.b = path;
                    parse(n.body, {"endfor"});
                    out.push_back(std::move(n));
                } else if (kw == "if") {
                    std::string cond; iss >> cond;
                    flush();
                    Node n; n.kind = Node::IF; n.a = cond;
                    std::string term = parse(n.body, {"else", "endif"});
                    if (term == "else") parse(n.elseBody, {"endif"});
                    out.push_back(std::move(n));
                }
                // unknown keyword → ignored
            } else {
                text += s[i++];
            }
        }
        flush();
        return "";
    }
};

// ── Renderer ────────────────────────────────────────────────────────────────
struct Ctx {
    const json& root;
    std::vector<std::pair<std::string, const json*>> locals;   // loop-scope bindings

    const json* lookup(const std::string& path) const {
        std::vector<std::string> parts = splitPath(path);
        if (parts.empty()) return nullptr;

        const json* base = nullptr;
        size_t start = 0;
        for (auto it = locals.rbegin(); it != locals.rend(); ++it)
            if (it->first == parts[0]) { base = it->second; start = 1; break; }
        if (!base) { base = &root; start = 0; }

        for (size_t k = start; k < parts.size(); ++k) {
            if (!base->is_object()) return nullptr;
            auto it = base->find(parts[k]);
            if (it == base->end()) return nullptr;
            base = &(*it);
        }
        return base;
    }
};

std::string toStr(const json& v) {
    if (v.is_string())          return v.get<std::string>();
    if (v.is_boolean())         return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer())  return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float())    { std::ostringstream o; o << v.get<double>(); return o.str(); }
    if (v.is_null())            return "";
    return v.dump();
}

bool truthy(const json* v) {
    if (!v || v->is_null())            return false;
    if (v->is_boolean())               return v->get<bool>();
    if (v->is_string())                return !v->get<std::string>().empty();
    if (v->is_array() || v->is_object()) return !v->empty();
    if (v->is_number())                return v->get<double>() != 0.0;
    return true;
}

void renderNodes(const std::vector<Node>& nodes, Ctx& ctx, std::string& out) {
    for (const Node& n : nodes) {
        switch (n.kind) {
            case Node::TEXT: out += n.a; break;
            case Node::VAR: {
                const json* v = ctx.lookup(n.a);
                if (v) out += toStr(*v);
                break;
            }
            case Node::FOR: {
                const json* list = ctx.lookup(n.b);
                if (list && list->is_array()) {
                    size_t count = list->size();
                    for (size_t k = 0; k < count; ++k) {
                        json loop = {
                            {"index", k + 1}, {"index0", k},
                            {"first", k == 0}, {"last", k + 1 == count}, {"count", count}
                        };
                        ctx.locals.push_back({n.a, &(*list)[k]});
                        ctx.locals.push_back({"loop", &loop});
                        renderNodes(n.body, ctx, out);
                        ctx.locals.pop_back();
                        ctx.locals.pop_back();
                    }
                }
                break;
            }
            case Node::IF: {
                if (truthy(ctx.lookup(n.a))) renderNodes(n.body, ctx, out);
                else                         renderNodes(n.elseBody, ctx, out);
                break;
            }
        }
    }
}

} // namespace

std::string render(const std::string& source, const json& data) {
    Parser p(source);
    std::vector<Node> nodes;
    p.parse(nodes, {});
    Ctx ctx{data, {}};
    std::string out;
    renderNodes(nodes, ctx, out);
    return out;
}

std::string renderFile(const std::string& path, const json& data, bool& ok) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { ok = false; return ""; }
    std::stringstream ss; ss << f.rdbuf();
    ok = true;
    return render(ss.str(), data);
}

} // namespace tmpl
