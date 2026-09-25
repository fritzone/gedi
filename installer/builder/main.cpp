// gedi-mkinstall - the install builder.
//
// Reads an install script (YAML), collects every file it names, compresses them,
// and welds the result onto the setup stub to produce a single self-contained
// installer executable.
//
//   gedi-mkinstall install.yaml [-o gedi-setup.exe] [--stub setup-stub.exe]
//                               [--root DIR] [-q]
//
// Paths inside the script are resolved against --root, which defaults to the
// directory holding the script.
#include "../common/Archive.h"
#include "../common/Yaml.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

bool g_quiet = false;

void info(const std::string& msg) { if (!g_quiet) std::cout << msg << "\n"; }
int  fail(const std::string& msg) { std::cerr << "error: " << msg << "\n"; return 1; }

std::string humanSize(uint64_t n) {
    char buf[64];
    if (n >= 1024ull * 1024)      std::snprintf(buf, sizeof buf, "%.1f MB", n / (1024.0 * 1024.0));
    else if (n >= 1024)           std::snprintf(buf, sizeof buf, "%.1f KB", n / 1024.0);
    else                          std::snprintf(buf, sizeof buf, "%llu B", (unsigned long long)n);
    return buf;
}

// Destination paths are always stored with forward slashes so the TOC is
// platform-neutral; the stub turns them back into native separators.
std::string toSlash(const std::string& p) {
    std::string s = p;
    std::replace(s.begin(), s.end(), '\\', '/');
    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    return s;
}

std::string joinDest(const std::string& dir, const std::string& name) {
    if (dir.empty()) return toSlash(name);
    return toSlash(dir) + "/" + toSlash(name);
}

// Shell-style match for the optional "pattern:" of a directory entry.
bool globMatch(const std::string& pat, const std::string& txt) {
    size_t p = 0, t = 0, star = std::string::npos, mark = 0;
    while (t < txt.size()) {
        if (p < pat.size() && (pat[p] == '?' || std::tolower((unsigned char)pat[p]) ==
                                                std::tolower((unsigned char)txt[t]))) {
            ++p; ++t;
        } else if (p < pat.size() && pat[p] == '*') {
            star = p++; mark = t;
        } else if (star != std::string::npos) {
            p = star + 1; t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

// One file to pack: where it comes from and where it lands.
struct Item {
    fs::path    source;
    std::string dest;        // install-dir relative, forward slashes
    std::string component;
};

// Expand a single "files:" entry, which is either one file (from/to) or a whole
// directory (dir/to, with an optional pattern and recurse flag).
bool expandEntry(const json& e, const fs::path& root, const std::string& comp,
                 std::vector<Item>& out, std::string& err) {
    if (e.contains("dir")) {
        const std::string rel   = e["dir"].get<std::string>();
        const std::string dest  = e.value("to", rel);
        const std::string pat   = e.value("pattern", std::string("*"));
        const bool recurse      = e.value("recurse", true);
        const fs::path dir      = root / rel;

        std::error_code ec;
        if (!fs::is_directory(dir, ec)) { err = "not a directory: " + dir.string(); return false; }

        auto consider = [&](const fs::directory_entry& de) {
            if (!de.is_regular_file()) return;
            const std::string name = de.path().filename().string();
            if (!globMatch(pat, name)) return;
            const fs::path sub = fs::relative(de.path(), dir, ec);
            out.push_back({ de.path(), joinDest(dest, sub.generic_string()), comp });
        };
        if (recurse) for (const auto& de : fs::recursive_directory_iterator(dir, ec)) consider(de);
        else         for (const auto& de : fs::directory_iterator(dir, ec))           consider(de);
        return true;
    }

    if (!e.contains("from")) { err = "files entry needs either 'from' or 'dir'"; return false; }
    const std::string from = e["from"].get<std::string>();
    const fs::path    src  = root / from;
    const std::string dest = e.value("to", fs::path(from).filename().string());

    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) { err = "missing file: " + src.string(); return false; }
    out.push_back({ src, toSlash(dest), comp });
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string script, out_path, stub_path, root_opt;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) { std::cerr << "error: " << what << " needs a value\n"; std::exit(1); }
            return argv[++i];
        };
        if      (a == "-o" || a == "--output") out_path  = next("-o");
        else if (a == "--stub")                stub_path = next("--stub");
        else if (a == "--root")                root_opt  = next("--root");
        else if (a == "-q" || a == "--quiet")  g_quiet   = true;
        else if (a == "-h" || a == "--help") {
            std::cout << "usage: gedi-mkinstall SCRIPT.yaml [-o OUT.exe] [--stub STUB.exe]\n"
                         "                      [--root DIR] [-q]\n";
            return 0;
        }
        else if (!a.empty() && a[0] == '-')    return fail("unknown option " + a);
        else if (script.empty())               script = a;
        else                                   return fail("unexpected argument " + a);
    }
    if (script.empty()) return fail("no install script given (try --help)");

    // ---- script -----------------------------------------------------------
    std::string yerr;
    json spec = yaml::parseFile(script, yerr);
    if (spec.is_discarded()) return fail(script + ": " + yerr);

    const fs::path root = root_opt.empty() ? fs::absolute(fs::path(script)).parent_path()
                                           : fs::path(root_opt);

    if (!spec.contains("product") || !spec["product"].contains("name"))
        return fail("script has no product.name");
    const std::string product = spec["product"]["name"].get<std::string>();
    const std::string version = spec["product"].value("version", std::string("0.0.0"));

    if (out_path.empty())
        out_path = (fs::path(script).parent_path() / (product + "-setup.exe")).string();
    if (stub_path.empty())
        stub_path = (fs::absolute(fs::path(argv[0])).parent_path() / "setup-stub.exe").string();

    info("install script : " + script);
    info("source root    : " + root.string());
    info("stub           : " + stub_path);

    // ---- collect the payload ----------------------------------------------
    std::vector<Item> items;
    if (!spec.contains("components") || !spec["components"].is_array())
        return fail("script has no components list");

    for (const auto& comp : spec["components"]) {
        const std::string id = comp.value("id", std::string());
        if (id.empty()) return fail("every component needs an id");
        if (!comp.contains("files")) continue;
        for (const auto& e : comp["files"]) {
            std::string err;
            if (!expandEntry(e, root, id, items, err)) return fail("component '" + id + "': " + err);
        }
    }
    if (items.empty()) return fail("the script selects no files at all");

    // A later entry for the same destination wins, so a component can override a
    // file another one laid down without the payload carrying it twice.
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.dest < b.dest; });
    items.erase(std::unique(items.begin(), items.end(),
                            [](const Item& a, const Item& b) { return a.dest == b.dest; }),
                items.end());

    // ---- stub --------------------------------------------------------------
    std::vector<uint8_t> image;
    if (!ar::readFile(stub_path, image))
        return fail("cannot read the setup stub: " + stub_path);
    const uint64_t payload_base = image.size();

    if (!ar::compressionAvailable())
        info("note: this build cannot compress - the payload will be stored verbatim");

    // ---- pack --------------------------------------------------------------
    json entries = json::array();
    uint64_t total_raw = 0, total_packed = 0;

    for (const Item& it : items) {
        std::vector<uint8_t> raw;
        if (!ar::readFile(it.source.string(), raw)) return fail("cannot read " + it.source.string());

        ar::Method m = ar::M_STORE;
        std::vector<uint8_t> packed = ar::compress(raw, m);

        json e;
        e["path"]      = it.dest;
        e["component"] = it.component;
        e["offset"]    = (uint64_t)(image.size() - payload_base);
        e["csize"]     = (uint64_t)packed.size();
        e["usize"]     = (uint64_t)raw.size();
        e["method"]    = (uint32_t)m;
        e["crc32"]     = ar::crc32(raw.data(), raw.size());
        entries.push_back(std::move(e));

        image.insert(image.end(), packed.begin(), packed.end());
        total_raw    += raw.size();
        total_packed += packed.size();

        if (!g_quiet)
            std::printf("  %-44s %9s -> %9s%s\n", it.dest.c_str(),
                        humanSize(raw.size()).c_str(), humanSize(packed.size()).c_str(),
                        m == ar::M_STORE ? "  (stored)" : "");
    }

    // ---- table of contents -------------------------------------------------
    // The TOC is the install script itself plus the packed extents, so the stub
    // has everything - ui text, components, shortcuts - from this one blob.
    json toc = spec;
    toc["entries"]     = std::move(entries);
    toc["total_bytes"] = total_raw;
    toc["built_with"]  = "gedi-mkinstall " + std::string("1.0");

    const std::string toc_text = toc.dump();
    std::vector<uint8_t> toc_raw(toc_text.begin(), toc_text.end());
    ar::Method toc_method = ar::M_STORE;
    std::vector<uint8_t> toc_packed = ar::compress(toc_raw, toc_method);

    ar::Trailer tr;
    tr.payload_base = payload_base;
    tr.toc_offset   = image.size();
    tr.toc_csize    = toc_packed.size();
    tr.toc_usize    = toc_raw.size();
    tr.toc_method   = (uint32_t)toc_method;
    tr.toc_crc32    = ar::crc32(toc_packed.data(), toc_packed.size());

    image.insert(image.end(), toc_packed.begin(), toc_packed.end());
    const uint8_t* trp = reinterpret_cast<const uint8_t*>(&tr);
    image.insert(image.end(), trp, trp + sizeof(tr));

    // ---- emit --------------------------------------------------------------
    std::error_code ec;
    fs::create_directories(fs::path(out_path).parent_path(), ec);
    if (!ar::writeFile(out_path, image.data(), image.size()))
        return fail("cannot write " + out_path);

    if (!g_quiet) {
        std::printf("\n%s %s\n", product.c_str(), version.c_str());
        std::printf("  %zu files, %s of content packed into %s\n",
                    items.size(), humanSize(total_raw).c_str(), humanSize(total_packed).c_str());
        std::printf("  installer: %s (%s)\n", out_path.c_str(), humanSize(image.size()).c_str());
    }
    return 0;
}
