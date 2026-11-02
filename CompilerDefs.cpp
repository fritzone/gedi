#include "CompilerDefs.h"
#include "CompilerSettings.h"
#include "DependencyChecker.h"
#include "ToolchainDetect.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace compilers {

struct RawDef {
    json     j;
    fs::path source;
    bool     user = false;
};

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// A compiler's file name without directory and without ".exe", lower-cased:
// what "executables" patterns are matched against.
std::string exeStem(const std::string& exe)
{
    std::string name = fs::path(exe).filename().string();
    const std::string l = lower(name);
    if (l.size() > 4 && l.compare(l.size() - 4, 4, ".exe") == 0) name.resize(name.size() - 4);
    return lower(name);
}

// '*' matches any run of characters; everything else literally.
bool globMatch(const std::string& pattern, const std::string& text)
{
    size_t p = 0, t = 0, star = std::string::npos, mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && pattern[p] == '*') { star = p++; mark = t; continue; }
        if (p < pattern.size() && pattern[p] == text[t]) { ++p; ++t; continue; }
        if (star == std::string::npos) return false;
        p = star + 1;
        t = ++mark;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

// Objects merge key by key, recursively; anything else in `over` replaces.
void deepMerge(json& base, const json& over)
{
    for (auto it = over.begin(); it != over.end(); ++it) {
        if (it.value().is_object() && base.contains(it.key()) && base[it.key()].is_object())
            deepMerge(base[it.key()], it.value());
        else
            base[it.key()] = it.value();
    }
}

const char* hostPlatform()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

std::string str(const json& j, const char* key, const std::string& fallback = "")
{
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : fallback;
}

bool boolean(const json& j, const char* key, bool fallback)
{
    auto it = j.find(key);
    return (it != j.end() && it->is_boolean()) ? it->get<bool>() : fallback;
}

std::vector<std::string> strings(const json& j, const char* key)
{
    std::vector<std::string> out;
    auto it = j.find(key);
    if (it != j.end() && it->is_array())
        for (const auto& v : *it) if (v.is_string()) out.push_back(v.get<std::string>());
    return out;
}

Option parseOption(const json& j)
{
    Option o;
    o.id    = str(j, "id");
    o.label = str(j, "label", o.id);
    o.flag  = str(j, "flag");
    o.cmake_flag = j.contains("cmake_flag") ? str(j, "cmake_flag") : o.flag;
    o.role  = str(j, "role");
    if (j.contains("choices") && j["choices"].is_array()) {
        for (const auto& c : j["choices"]) {
            Choice ch;
            ch.label = str(c, "label");
            ch.flag  = str(c, "flag");
            ch.cmake_flag = c.contains("cmake_flag") ? str(c, "cmake_flag") : ch.flag;
            o.choices.push_back(ch);
        }
    }
    if (j.contains("default")) {
        const json& d = j["default"];
        if (d.is_boolean())           o.def = d.get<bool>() ? 1 : 0;
        else if (d.is_number_integer()) o.def = d.get<int>();
    }
    if (o.radio() && (o.def < 0 || o.def >= (int)o.choices.size())) o.def = 0;
    return o;
}

Definition parseDefinition(const std::string& id, const json& j, const RawDef& raw)
{
    Definition d;
    d.id          = id;
    d.name        = str(j, "name", id);
    d.description = str(j, "description");
    d.source      = raw.source;
    d.user        = raw.user;
    d.abstract    = boolean(j, "abstract", false);
    if (j.contains("order") && j["order"].is_number_integer()) d.order = j["order"].get<int>();
    d.platforms   = strings(j, "platforms");
    d.setup       = str(j, "setup", "plain");
    d.settings_id = str(j, "settings", id);

    if (j.contains("match") && j["match"].is_object()) {
        const json& m = j["match"];
        d.executables   = strings(m, "executables");
        for (auto& e : d.executables) e = lower(e);
        d.version_args  = str(m, "version_args", "--version");
        d.version_regex = str(m, "version_regex");
        d.target        = str(m, "target");
        if (m.contains("priority") && m["priority"].is_number_integer()) d.priority = m["priority"].get<int>();
    }

    if (j.contains("command") && j["command"].is_object()) {
        const json& c = j["command"];
        d.args           = strings(c, "args");
        d.always         = strings(c, "always");
        d.output         = str(c, "output", d.output);
        d.compile_only   = str(c, "compile_only", d.compile_only);
        d.object_output  = str(c, "object_output", d.object_output);
        d.include        = str(c, "include", d.include);
        d.define         = str(c, "define", d.define);
        d.exe_ext        = str(c, "exe_ext", d.exe_ext);
        d.host_libraries = boolean(c, "host_libraries", d.host_libraries);
        d.scratch_dir    = boolean(c, "scratch_dir", d.scratch_dir);
        d.keep_outputs   = strings(c, "keep_outputs");
        d.standard_flag  = str(c, "standard_flag");
        d.standards      = strings(c, "standards");
        if (c.contains("standard_flags") && c["standard_flags"].is_object())
            for (auto it = c["standard_flags"].begin(); it != c["standard_flags"].end(); ++it)
                if (it.value().is_string()) d.standard_flags[it.key()] = it.value().get<std::string>();
    }

    if (j.contains("capabilities") && j["capabilities"].is_object()) {
        const json& c = j["capabilities"];
        d.runs_on_host  = boolean(c, "runs_on_host", d.runs_on_host);
        d.debuggable    = boolean(c, "debuggable", d.debuggable);
        d.parse_headers = str(c, "parse_headers", d.parse_headers);
        d.make          = boolean(c, "make", d.make);
        d.meson         = boolean(c, "meson", d.meson);
        if (c.contains("cmake")) {
            const json& cm = c["cmake"];
            if (cm.is_boolean()) d.cmake.supported = cm.get<bool>();
            else if (cm.is_object()) {
                d.cmake.supported      = boolean(cm, "supported", true);
                d.cmake.generator      = str(cm, "generator");
                d.cmake.compiler       = str(cm, "compiler");
                d.cmake.build_dir      = str(cm, "build_dir", "build");
                d.cmake.optional_flags = boolean(cm, "optional_flags", true);
            }
        }
    }

    if (j.contains("diagnostics")) {
        const json& g = j["diagnostics"];
        if (g.is_string()) d.diagnostics = g.get<std::string>();
        else if (g.is_object()) {
            d.diagnostics   = "regex";
            d.diag_regex    = str(g, "regex");
            auto num = [&](const char* k, int fb) {
                return (g.contains(k) && g[k].is_number_integer()) ? g[k].get<int>() : fb;
            };
            d.diag_file     = num("file", 1);
            d.diag_line     = num("line", 2);
            d.diag_col      = num("column", 0);
            d.diag_severity = num("severity", 3);
            d.diag_message  = num("message", 4);
        }
    }

    if (j.contains("top_option") && j["top_option"].is_object()) {
        Option top = parseOption(j["top_option"]);
        if (top.radio()) d.top = top;
    }

    std::set<std::string> removed;
    for (const auto& r : strings(j, "remove_options")) removed.insert(r);

    auto parseTabs = [&](const json& arr) {
        for (const auto& tj : arr) {
            Tab tab;
            tab.name = str(tj, "tab", "Options");
            if (tj.contains("groups") && tj["groups"].is_array()) {
                for (const auto& gj : tj["groups"]) {
                    Group grp;
                    grp.name = str(gj, "name");
                    if (gj.contains("items") && gj["items"].is_array())
                        for (const auto& oj : gj["items"]) {
                            Option o = parseOption(oj);
                            if (!o.id.empty() && !removed.count(o.id)) grp.options.push_back(o);
                        }
                    if (!grp.options.empty()) tab.groups.push_back(grp);
                }
            }
            if (!tab.groups.empty()) d.tabs.push_back(tab);
        }
    };
    // The catalogue: "options" (replaced as a whole by an heir that gives its
    // own), then "extra_options" - tabs added to an inherited catalogue.
    if (j.contains("options") && j["options"].is_array())             parseTabs(j["options"]);
    if (j.contains("extra_options") && j["extra_options"].is_array()) parseTabs(j["extra_options"]);
    if (d.top && removed.count(d.top->id)) d.top.reset();

    // The top row belongs to the C++ standard whenever the compiler has a
    // standard switch; a top option then leads the first tab instead.
    if (d.top && (!d.standard_flag.empty() || !d.standard_flags.empty())) {
        Group g;
        g.name = d.top->label;
        g.options.push_back(*d.top);
        if (d.tabs.empty()) d.tabs.push_back(Tab{ "Options", {} });
        d.tabs.front().groups.insert(d.tabs.front().groups.begin(), g);
        d.top.reset();
    }
    return d;
}

// The minimal GCC definition used when no definition files can be found, so
// the editor still compiles something.
Definition builtinGcc()
{
    Definition d;
    d.id = "gcc"; d.name = "GCC"; d.setup = "gcc"; d.settings_id = "gnu"; d.order = 10;
    d.executables   = { "g++", "c++" };
    d.standard_flag = "-std={std}";
    d.standards     = { "c++11", "c++14", "c++17", "c++20", "c++23", "c++26" };
    return d;
}

std::string runOutput(const std::string& exe, const std::string& args)
{
    return toolchain::runOutput(exe, args);
}

fs::path testsPath()
{
    return fs::path(DependencyChecker::toolchainPath()).parent_path() / "option-tests.json";
}

long long exeStamp(const std::string& exe)
{
    std::error_code ec;
    const auto t = fs::last_write_time(exe, ec);
    if (ec) return 0;
    return (long long)t.time_since_epoch().count();
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Definition helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string Definition::standardFlag(const std::string& standard) const
{
    auto it = standard_flags.find(standard);
    if (it != standard_flags.end()) return it->second;
    if (standard_flag.empty()) return "";
    std::string f = standard_flag;
    const size_t at = f.find("{std}");
    if (at != std::string::npos) f.replace(at, 5, standard);
    return f;
}

std::string Definition::argsString() const
{
    std::string s;
    for (const auto& a : args) s += " " + a;
    return s;
}

std::string Definition::exeExtension() const
{
    if (exe_ext != "host") return exe_ext;
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}

std::string Definition::outputPrefix() const
{
    const size_t at = output.find("{out}");
    return at == std::string::npos ? output : output.substr(0, at);
}

const Option* Definition::findOption(const std::string& oid) const
{
    for (const Option* o : allOptions())
        if (o->id == oid) return o;
    return nullptr;
}

std::vector<const Option*> Definition::allOptions() const
{
    std::vector<const Option*> out;
    if (top) out.push_back(&*top);
    for (const auto& t : tabs)
        for (const auto& g : t.groups)
            for (const auto& o : g.options) out.push_back(&o);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Settings
// ─────────────────────────────────────────────────────────────────────────────

int optionValue(const CompilerSettings& s, const Definition& d, const Option& o)
{
    int v = o.def;
    auto c = s.compilers.find(d.settings_id);
    if (c != s.compilers.end()) {
        auto it = c->second.values.find(o.id);
        if (it != c->second.values.end()) v = it->second;
    }
    if (o.radio() && (v < 0 || v >= (int)o.choices.size())) v = o.def;
    return v;
}

void fillDefaults(CompilerSettings& s, const Definition& d)
{
    CompilerOptionValues& vals = s.compilers[d.settings_id];
    for (const Option* o : d.allOptions())
        if (!vals.values.count(o->id)) vals.values[o->id] = o->def;
}

const std::string& optionalFlags(const CompilerSettings& s, const Definition& d)
{
    static const std::string none;
    auto c = s.compilers.find(d.settings_id);
    return c == s.compilers.end() ? none : c->second.optional_flags;
}

std::string flagsFor(const CompilerSettings& s, const Definition& d, bool for_cmake)
{
    std::string f;
    auto add = [&](const std::string& x) {
        if (x.empty()) return;
        if (!f.empty()) f += ' ';
        f += x;
    };
    if (!for_cmake)
        for (const auto& a : d.always) add(a);
    for (const Option* o : d.allOptions()) {
        const int v = optionValue(s, d, *o);
        if (o->radio()) add(for_cmake ? o->choices[v].cmake_flag : o->choices[v].flag);
        else if (v)     add(for_cmake ? o->cmake_flag : o->flag);
    }
    if (!for_cmake || d.cmake.optional_flags) add(optionalFlags(s, d));
    return f;
}

std::string cmakeBuildType(const CompilerSettings& s, const Definition& d)
{
    bool debug = false;
    int  opt   = 0;
    for (const Option* o : d.allOptions()) {
        if (o->role == "debug")        debug = optionValue(s, d, *o) != 0;
        if (o->role == "optimization") opt   = optionValue(s, d, *o);
    }
    if (debug && opt > 0) return "RelWithDebInfo";
    if (!debug && opt > 0) return "Release";
    return "Debug";
}

std::set<std::string> testedUnsupported(const Definition& d, const std::string& exe)
{
    std::set<std::string> out;
    try {
        std::ifstream in(testsPath());
        if (!in) return out;
        json j = json::parse(in);
        const std::string key = d.id + "|" + exe;
        if (!j.contains(key)) return out;
        const json& e = j[key];
        if (e.value("stamp", 0LL) != exeStamp(exe)) return out;   // the compiler changed since
        for (const auto& u : e.value("unsupported", std::vector<std::string>{})) out.insert(u);
    } catch (...) {}
    return out;
}

void saveTestedUnsupported(const Definition& d, const std::string& exe,
                           const std::set<std::string>& unsupported)
{
    json j = json::object();
    try {
        std::ifstream in(testsPath());
        if (in) { json e = json::parse(in); if (e.is_object()) j = std::move(e); }
    } catch (...) {}
    j[d.id + "|" + exe] = {
        {"stamp", exeStamp(exe)},
        {"unsupported", std::vector<std::string>(unsupported.begin(), unsupported.end())},
    };
    std::error_code ec;
    fs::create_directories(testsPath().parent_path(), ec);
    std::ofstream out(testsPath(), std::ios::trunc);
    if (out) out << j.dump(4) << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Target platform
// ─────────────────────────────────────────────────────────────────────────────

bool targetIsHost(const std::string& triple_in)
{
    const std::string t = lower(triple_in);
    if (t.empty()) return true;            // it would not say: assume a native compiler

#if defined(__x86_64__) || defined(_M_X64)
    const bool arch = t.rfind("x86_64", 0) == 0 || t.rfind("amd64", 0) == 0 ||
                      t.rfind("i386", 0) == 0 || t.rfind("i486", 0) == 0 ||
                      t.rfind("i586", 0) == 0 || t.rfind("i686", 0) == 0;
#elif defined(__i386__) || defined(_M_IX86)
    const bool arch = t.rfind("i386", 0) == 0 || t.rfind("i486", 0) == 0 ||
                      t.rfind("i586", 0) == 0 || t.rfind("i686", 0) == 0;
#elif defined(__aarch64__) || defined(_M_ARM64)
    const bool arch = t.rfind("aarch64", 0) == 0 || t.rfind("arm64", 0) == 0;
#else
    const bool arch = true;
#endif

#if defined(_WIN32)
    const bool os = t.find("mingw") != std::string::npos || t.find("windows") != std::string::npos ||
                    t.find("cygwin") != std::string::npos || t.find("msvc") != std::string::npos;
#elif defined(__APPLE__)
    const bool os = t.find("darwin") != std::string::npos || t.find("apple") != std::string::npos;
#else
    const bool os = t.find("linux") != std::string::npos;
#endif
    return arch && os;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Registry
// ─────────────────────────────────────────────────────────────────────────────

Registry& Registry::instance()
{
    static Registry r;
    return r;
}

fs::path Registry::userDir() const
{
    return fs::path(DependencyChecker::toolchainPath()).parent_path() / "compilers";
}

void Registry::loadDir(const fs::path& dir, bool user, std::map<std::string, RawDef>& raw)
{
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());

    for (const auto& f : files) {
        try {
            std::ifstream in(f);
            json j = json::parse(in, nullptr, true, /*ignore_comments=*/true);
            if (!j.is_object()) { problems_.push_back(f.string() + ": not a JSON object"); continue; }
            std::string id = str(j, "id", f.stem().string());
            raw[id] = RawDef{ j, f, user };    // a user file replaces a shipped one
        } catch (const std::exception& e) {
            problems_.push_back(f.string() + ": " + e.what());
        }
    }
}

void Registry::load(const fs::path& exe_dir)
{
    exe_dir_ = exe_dir;
    reload();
}

void Registry::reload()
{
    loaded_ = true;
    defs_.clear();
    problems_.clear();

    // The shipped definitions: the first of the usual data directories that has them.
    std::vector<fs::path> shipped;
    if (!exe_dir_.empty()) {
        shipped.push_back(exe_dir_ / "compilers");
        shipped.push_back(exe_dir_.parent_path() / "compilers");
        shipped.push_back(exe_dir_.parent_path() / "share/gedi/compilers");
    }
    shipped.push_back("/usr/share/gedi/compilers");
    shipped.push_back("/usr/local/share/gedi/compilers");
    shipped.push_back("compilers");

    std::map<std::string, RawDef> raw;
    std::error_code ec;
    for (const auto& dir : shipped)
        if (fs::is_directory(dir, ec)) { loadDir(dir, false, raw); break; }
    loadDir(userDir(), true, raw);

    // Resolve inheritance: the parent's resolved JSON, with the child merged
    // over it. A definition that declares options owns its settings; heirs
    // that do not share them.
    std::map<std::string, json> resolved;
    std::function<const json*(const std::string&, int)> resolve =
        [&](const std::string& id, int depth) -> const json* {
            auto done = resolved.find(id);
            if (done != resolved.end()) return &done->second;
            auto it = raw.find(id);
            if (it == raw.end() || depth > 16) return nullptr;
            const json& child = it->second.j;
            json merged = json::object();
            const std::string parent = str(child, "inherits");
            if (!parent.empty()) {
                const json* p = resolve(parent, depth + 1);
                if (!p) problems_.push_back(it->second.source.string() +
                                            ": inherits unknown definition \"" + parent + "\"");
                else merged = *p;
            }
            merged.erase("abstract");           // being a base is not inherited
            deepMerge(merged, child);
            // Only a whole catalogue of its own ("options") gets its own stored
            // values; a top option or extra tabs keep sharing the parent's, so
            // the common options carry over between related compilers.
            if (child.contains("options") && !child.contains("settings"))
                merged["settings"] = id;
            if (!merged.contains("settings")) merged["settings"] = id;
            return &(resolved[id] = merged);
        };

    for (const auto& [id, r] : raw) {
        const json* j = resolve(id, 0);
        if (!j) continue;
        Definition d = parseDefinition(id, *j, r);
        if (!d.platforms.empty() &&
            std::find(d.platforms.begin(), d.platforms.end(), hostPlatform()) == d.platforms.end())
            continue;                           // e.g. MSVC away from Windows
        defs_.push_back(std::move(d));
    }

    if (!find("gcc")) {
        problems_.push_back("no compiler definitions found; using a minimal built-in GCC");
        defs_.push_back(builtinGcc());
    }

    std::stable_sort(defs_.begin(), defs_.end(), [](const Definition& a, const Definition& b) {
        if (a.order != b.order) return a.order < b.order;
        return a.name < b.name;
    });
}

std::vector<const Definition*> Registry::selectable() const
{
    if (!loaded_) const_cast<Registry*>(this)->reload();
    std::vector<const Definition*> out;
    for (const auto& d : defs_) if (!d.abstract) out.push_back(&d);
    return out;
}

const Definition* Registry::find(const std::string& id) const
{
    for (const auto& d : defs_) if (d.id == id) return &d;
    return nullptr;
}

const Definition* Registry::identify(const std::string& exe) const
{
    if (!loaded_) const_cast<Registry*>(this)->reload();
    if (exe.empty()) return nullptr;
    const std::string stem = exeStem(exe);

    // Candidates by name: an exact name before a wildcard pattern, the user's
    // own definitions before shipped ones, then list order.
    struct Cand { const Definition* d; int quality; };
    std::vector<Cand> cands;
    for (const Definition* d : selectable()) {
        int q = 0;
        for (const auto& pat : d->executables) {
            if (pat == stem) { q = 2; break; }
            if (pat.find('*') != std::string::npos && globMatch(pat, stem)) q = std::max(q, 1);
        }
        if (q) cands.push_back({ d, q });
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.quality != b.quality) return a.quality > b.quality;
        if (a.d->user != b.d->user) return a.d->user > b.d->user;
        return a.d->priority > b.d->priority;   // e.g. AOCC before plain Clang
    });

    std::map<std::string, std::string> outputs;   // by arguments: run each only once
    auto output = [&](const std::string& args) -> const std::string& {
        auto it = outputs.find(args);
        if (it == outputs.end()) it = outputs.emplace(args, runOutput(exe, args)).first;
        return it->second;
    };

    for (const auto& c : cands) {
        const Definition& d = *c.d;
        if (!d.version_regex.empty()) {
            try {
                if (!std::regex_search(output(d.version_args), std::regex(d.version_regex)))
                    continue;
            } catch (const std::regex_error&) { continue; }
        }
        if (!d.target.empty()) {
            std::string triple = output("-dumpmachine");
            triple.erase(triple.find_last_not_of(" \t\r\n") + 1);
            const bool host = targetIsHost(triple);
            if ((d.target == "host") != host) continue;
        }
        return &d;
    }
    return nullptr;
}

const Definition& Registry::forToolchain(const std::string& compiler_id, const std::string& kind,
                                         const std::string& cxx) const
{
    if (!loaded_) const_cast<Registry*>(this)->reload();
    if (const Definition* d = find(compiler_id)) return *d;
    if (const Definition* d = identify(cxx)) return *d;
    // A toolchain.json from before definitions existed names only its kind.
    if (const Definition* d = find(kind)) return *d;
    return *find("gcc");
}

std::string Registry::findOnPath(const Definition& d) const
{
    const char* path_env = std::getenv("PATH");
    if (!path_env) return {};
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::stringstream ss(path_env);
    std::vector<std::string> dirs;
    for (std::string dir; std::getline(ss, dir, sep); ) if (!dir.empty()) dirs.push_back(dir);

    // Candidates: exact names first, in PATH order, then wildcard matches. Only
    // one that identifies as this very definition is offered - "g++" is on PATH
    // but it is no cross compiler, and a cross compiler's own name is only
    // known by its pattern (arm-none-eabi-g++ for "*-g++").
    std::vector<std::string> exact, wild;
    std::error_code ec;
    for (const auto& dir : dirs) {
        for (const auto& name : d.executables) {
            if (name.find('*') != std::string::npos) continue;
            for (const char* ext : { "", ".exe" }) {
                const fs::path c = fs::path(dir) / (name + ext);
                if (fs::is_regular_file(c, ec)) exact.push_back(c.string());
            }
        }
    }
    for (const auto& dir : dirs) {
        if (!fs::is_directory(dir, ec)) continue;
        std::vector<std::string> found;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            const std::string stem = exeStem(e.path().string());
            for (const auto& pat : d.executables)
                if (pat.find('*') != std::string::npos && globMatch(pat, stem)) {
                    found.push_back(e.path().string());
                    break;
                }
        }
        std::sort(found.begin(), found.end());
        wild.insert(wild.end(), found.begin(), found.end());
    }

    int tried = 0;
    for (const auto* list : { &exact, &wild })
        for (const auto& c : *list) {
            if (++tried > 12) return {};            // identifying runs each one: keep it quick
            if (identify(c) == &d) return c;
        }
    return {};
}

std::string Registry::addUser(const std::string& name, const std::string& base,
                              const std::string& executable, bool cross, std::string& error)
{
    error.clear();
    if (name.empty()) { error = "Give the compiler a name."; return {}; }
    if (!find(base))  { error = "Unknown base definition \"" + base + "\"."; return {}; }

    std::string slug;
    for (char c : lower(name)) {
        if (std::isalnum((unsigned char)c)) slug += c;
        else if (!slug.empty() && slug.back() != '-') slug += '-';
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    if (slug.empty()) slug = "compiler";

    std::string id = "user-" + slug;
    for (int n = 2; find(id); ++n) id = "user-" + slug + "-" + std::to_string(n);

    json j = {
        {"id", id},
        {"name", name},
        {"inherits", base},
        {"description", "Introduced in Options > Compiler Options; edit this file to refine it."},
        {"order", 200},
        // Its own option values, not the base's: it is a different compiler.
        {"settings", id},
        // Recognised by its own file name only: the base's version and target
        // checks were written for a different compiler.
        {"match", {{"executables", json::array({ exeStem(executable) })},
                   {"version_regex", ""}, {"target", ""}}},
    };
    if (cross)
        j["capabilities"] = {{"runs_on_host", false}, {"debuggable", false}};
    if (cross)
        j["command"] = {{"host_libraries", false}};

    std::error_code ec;
    fs::create_directories(userDir(), ec);
    const fs::path file = userDir() / (id + ".json");
    std::ofstream out(file, std::ios::trunc);
    if (!out) { error = "Cannot write " + file.string(); return {}; }
    out << j.dump(4) << "\n";
    out.close();

    reload();
    if (!find(id)) { error = "The new definition could not be read back from " + file.string(); return {}; }
    return id;
}

} // namespace compilers
