#include "GediProject.h"

#include <filesystem>
#include <fstream>
#include <string>

//  helpers 

static std::string trim(const std::string& s)
{
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::pair<std::string, std::string> splitKV(const std::string& line)
{
    const auto colon = line.find(':');
    if (colon == std::string::npos) return {{}, trim(line)};
    return {trim(line.substr(0, colon)), trim(line.substr(colon + 1))};
}

static bool parseBool(const std::string& v) { return v == "1" || v == "true"; }

//  GediProject 

std::string GediProject::projectFilePath() const
{
    return (std::filesystem::path(root) / (name + ".gproj")).string();
}

bool GediProject::save() const
{
    std::ofstream f(projectFilePath());
    if (!f) return false;

    f << "# gedi project file\n";
    f << "version: "      << version      << "\n";
    f << "name: "         << name         << "\n";
    f << "build_system: " << build_system << "\n";
    // cpp_standard kept in header for legacy readers; authoritative copy is in [compiler_settings]
    f << "cpp_standard: " << compiler_settings.cpp_standard << "\n";

    // Save targets (new format)
    for (const auto& tgt : targets) {
        f << "\n[target]\n";
        f << "name: " << tgt.name << "\n";
        f << "type: " << tgt.type << "\n";
        for (const auto& s : tgt.sources)
            f << "source: " << s << "\n";
        for (const auto& lt : tgt.link_targets)
            f << "link_target: " << lt << "\n";
    }

    //  Compiler settings 
    const CompilerSettings& cs = compiler_settings;
    f << "\n[compiler_settings]\n";
    f << "cpp_standard: "    << cs.cpp_standard    << "\n";
    f << "debug_symbols: "   << cs.debug_symbols   << "\n";
    f << "optimization_level: " << cs.optimization_level << "\n";
    f << "wall: "            << cs.wall            << "\n";
    f << "wextra: "          << cs.wextra          << "\n";
    f << "wpedantic: "       << cs.wpedantic       << "\n";
    f << "werror: "          << cs.werror          << "\n";
    f << "wconversion: "     << cs.wconversion     << "\n";
    f << "wsign_conversion: "<< cs.wsign_conversion<< "\n";
    f << "wshadow: "         << cs.wshadow         << "\n";
    f << "wnon_virtual_dtor: "<< cs.wnon_virtual_dtor<< "\n";
    f << "wold_style_cast: " << cs.wold_style_cast << "\n";
    f << "woverloaded_virtual: " << cs.woverloaded_virtual << "\n";
    f << "wnull_dereference: "<< cs.wnull_dereference<< "\n";
    f << "wdouble_promotion: "<< cs.wdouble_promotion<< "\n";
    f << "wformat_2: "       << cs.wformat_2       << "\n";
    f << "fno_omit_frame_pointer: " << cs.fno_omit_frame_pointer << "\n";
    f << "fsanitize_address_ub: "   << cs.fsanitize_address_ub   << "\n";
    f << "fsanitize_leak: "  << cs.fsanitize_leak  << "\n";
    f << "flto: "            << cs.flto            << "\n";
    f << "march_native: "    << cs.march_native    << "\n";
    f << "mtune_native: "    << cs.mtune_native    << "\n";
    f << "wcast_align: "     << cs.wcast_align     << "\n";
    f << "wcast_qual: "      << cs.wcast_qual      << "\n";
    f << "wswitch_enum: "    << cs.wswitch_enum    << "\n";
    f << "wundef: "          << cs.wundef          << "\n";
    f << "wredundant_decls: "<< cs.wredundant_decls<< "\n";
    f << "wlogical_op: "     << cs.wlogical_op     << "\n";
    f << "wuseless_cast: "   << cs.wuseless_cast   << "\n";
    f << "weffcxx: "         << cs.weffcxx         << "\n";
    f << "fno_exceptions: "  << cs.fno_exceptions  << "\n";
    f << "fno_rtti: "        << cs.fno_rtti        << "\n";
    f << "fvisibility_hidden: " << cs.fvisibility_hidden << "\n";
    f << "fstrict_aliasing: "<< cs.fstrict_aliasing<< "\n";
    f << "fsanitize_pointer_compare: " << cs.fsanitize_pointer_compare << "\n";
    f << "fsanitize_pointer_subtract: " << cs.fsanitize_pointer_subtract << "\n";
    f << "wl_as_needed: "    << cs.wl_as_needed    << "\n";
    f << "wl_o1: "           << cs.wl_o1           << "\n";
    if (!cs.optional_flags.empty())
        f << "optional_flags: " << cs.optional_flags << "\n";

    for (const auto& lib : libraries) {
        f << "\n[library]\n";
        f << "short_name: "              << lib.short_name              << "\n";
        f << "version: "                 << lib.version                 << "\n";
        f << "cmake_find_package_hint: " << lib.cmake_find_package_hint << "\n";
        for (const auto& d : lib.include_directories)  f << "include_dir: "    << d << "\n";
        for (const auto& l : lib.link_libraries)       f << "link_lib: "       << l << "\n";
        for (const auto& d : lib.link_directories)     f << "link_dir: "       << d << "\n";
        for (const auto& g : lib.compiler_flags)       f << "compiler_flag: "  << g << "\n";
    }

    return true;
}

bool GediProject::load(const std::string& path, GediProject& out)
{
    std::ifstream f(path);
    if (!f) return false;

    GediProject proj;
    proj.root = std::filesystem::path(path).parent_path().string();

    enum class Section { Header, Sources, CompilerSettings, Library, Target } section = Section::Header;
    LibraryInfo  current_lib;
    ProjectTarget current_target;
    bool         in_lib    = false;
    bool         in_target = false;

    auto flush_lib = [&]() {
        if (in_lib && !current_lib.short_name.empty())
            proj.libraries.push_back(std::move(current_lib));
        current_lib = {};
        in_lib = false;
    };

    auto flush_target = [&]() {
        if (in_target && !current_target.name.empty())
            proj.targets.push_back(std::move(current_target));
        current_target = {};
        in_target = false;
    };

    std::string line;
    while (std::getline(f, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        if (t[0] == '[') {
            const auto end = t.find(']');
            const std::string sec = (end != std::string::npos)
                                    ? trim(t.substr(1, end - 1)) : "";
            if (sec == "sources") {
                flush_lib(); flush_target(); section = Section::Sources;
            } else if (sec == "compiler_settings") {
                flush_lib(); flush_target(); section = Section::CompilerSettings;
            } else if (sec == "library") {
                flush_lib(); flush_target(); section = Section::Library; in_lib = true;
            } else if (sec == "target") {
                flush_lib(); flush_target(); section = Section::Target; in_target = true;
            }
            continue;
        }

        if (section == Section::Sources) {
            proj.sources.push_back(t);
            continue;
        }

        const auto [key, val] = splitKV(t);

        if (section == Section::Header) {
            if      (key == "version")      proj.version      = val.empty() ? 1 : std::stoi(val);
            else if (key == "name")         proj.name         = val;
            else if (key == "build_system") proj.build_system = val;
            else if (key == "cpp_standard") {
                proj.cpp_standard = val;
                proj.compiler_settings.cpp_standard = val;  // legacy compat
            }
        } else if (section == Section::CompilerSettings) {
            CompilerSettings& cs = proj.compiler_settings;
            if      (key == "cpp_standard")    cs.cpp_standard    = val;
            else if (key == "debug_symbols")   cs.debug_symbols   = parseBool(val);
            else if (key == "optimization_level") cs.optimization_level = val.empty() ? 0 : std::stoi(val);
            else if (key == "wall")            cs.wall            = parseBool(val);
            else if (key == "wextra")          cs.wextra          = parseBool(val);
            else if (key == "wpedantic")       cs.wpedantic       = parseBool(val);
            else if (key == "werror")          cs.werror          = parseBool(val);
            else if (key == "wconversion")     cs.wconversion     = parseBool(val);
            else if (key == "wsign_conversion")cs.wsign_conversion= parseBool(val);
            else if (key == "wshadow")         cs.wshadow         = parseBool(val);
            else if (key == "wnon_virtual_dtor")cs.wnon_virtual_dtor = parseBool(val);
            else if (key == "wold_style_cast") cs.wold_style_cast = parseBool(val);
            else if (key == "woverloaded_virtual") cs.woverloaded_virtual = parseBool(val);
            else if (key == "wnull_dereference") cs.wnull_dereference = parseBool(val);
            else if (key == "wdouble_promotion") cs.wdouble_promotion = parseBool(val);
            else if (key == "wformat_2")       cs.wformat_2       = parseBool(val);
            else if (key == "fno_omit_frame_pointer") cs.fno_omit_frame_pointer = parseBool(val);
            else if (key == "fsanitize_address_ub")   cs.fsanitize_address_ub   = parseBool(val);
            else if (key == "fsanitize_leak")  cs.fsanitize_leak  = parseBool(val);
            else if (key == "flto")            cs.flto            = parseBool(val);
            else if (key == "march_native")    cs.march_native    = parseBool(val);
            else if (key == "mtune_native")    cs.mtune_native    = parseBool(val);
            else if (key == "wcast_align")     cs.wcast_align     = parseBool(val);
            else if (key == "wcast_qual")      cs.wcast_qual      = parseBool(val);
            else if (key == "wswitch_enum")    cs.wswitch_enum    = parseBool(val);
            else if (key == "wundef")          cs.wundef          = parseBool(val);
            else if (key == "wredundant_decls")cs.wredundant_decls= parseBool(val);
            else if (key == "wlogical_op")     cs.wlogical_op     = parseBool(val);
            else if (key == "wuseless_cast")   cs.wuseless_cast   = parseBool(val);
            else if (key == "weffcxx")         cs.weffcxx         = parseBool(val);
            else if (key == "fno_exceptions")  cs.fno_exceptions  = parseBool(val);
            else if (key == "fno_rtti")        cs.fno_rtti        = parseBool(val);
            else if (key == "fvisibility_hidden") cs.fvisibility_hidden = parseBool(val);
            else if (key == "fstrict_aliasing")cs.fstrict_aliasing= parseBool(val);
            else if (key == "fsanitize_pointer_compare") cs.fsanitize_pointer_compare = parseBool(val);
            else if (key == "fsanitize_pointer_subtract") cs.fsanitize_pointer_subtract = parseBool(val);
            else if (key == "wl_as_needed")    cs.wl_as_needed    = parseBool(val);
            else if (key == "wl_o1")           cs.wl_o1           = parseBool(val);
            else if (key == "optional_flags")  cs.optional_flags  = val;
        } else if (section == Section::Library) {
            if      (key == "short_name")              current_lib.short_name              = val;
            else if (key == "version")                 current_lib.version                 = val;
            else if (key == "cmake_find_package_hint") current_lib.cmake_find_package_hint = val;
            else if (key == "include_dir")             current_lib.include_directories.push_back(val);
            else if (key == "link_lib")                current_lib.link_libraries.push_back(val);
            else if (key == "link_dir")                current_lib.link_directories.push_back(val);
            else if (key == "compiler_flag")           current_lib.compiler_flags.push_back(val);
        } else if (section == Section::Target) {
            if      (key == "name")        current_target.name = val;
            else if (key == "type")        current_target.type = val;
            else if (key == "source")      current_target.sources.push_back(val);
            else if (key == "link_target") current_target.link_targets.push_back(val);
        }
    }

    flush_lib();
    flush_target();

    // Migration: old .gproj files use flat [sources]; convert to one executable target
    if (proj.targets.empty() && !proj.sources.empty()) {
        ProjectTarget t;
        t.name    = proj.name;
        t.sources = proj.sources;
        proj.targets.push_back(std::move(t));
    }

    out = std::move(proj);
    return true;
}

std::string GediProject::buildFile() const
{
    if (build_system == "cmake") return "CMakeLists.txt";
    else if (build_system == "make")  return "Makefile";
    else if (build_system == "meson") return "meson.build";
    else return "unknown";
}

std::string GediProject::buildFilePath() const
{
   return (std::filesystem::path(root) / buildFile() ).string();
}

bool GediProject::removeFileFromBuildSystem(const std::string &file_to_remove, const std::string& rel)
{
    if (build_system == "cmake") {
        return removeFileFromCMakeLists(file_to_remove, rel);
    } else if (build_system == "make") {
        return removeFileFromMakefile(file_to_remove, rel);
    } else if (build_system == "meson") {
        return removeFileFromMesonBuild(file_to_remove, rel);
    }
    return false;
}

bool GediProject::removeFileFromCMakeLists(const std::string &cmake_path, const std::string &rel_path)
{
    std::ifstream fin(cmake_path);
    if (!fin) return false;
    std::string content((std::istreambuf_iterator<char>(fin)), {});
    fin.close();

    size_t pos = content.find(rel_path);
    if (pos == std::string::npos) return false;

    // If the filename occupies its own line (only whitespace before it), remove the whole line
    size_t line_start = content.rfind('\n', pos);
    line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
    size_t line_end = content.find('\n', pos);
    if (line_end == std::string::npos) line_end = content.size();

    std::string before_on_line = content.substr(line_start, pos - line_start);
    bool only_ws = before_on_line.find_first_not_of(" \t") == std::string::npos;
    std::string after_on_line = content.substr(pos + rel_path.size(),
                                               line_end - pos - rel_path.size());
    bool rest_ws = after_on_line.find_first_not_of(" \t") == std::string::npos;

    if (only_ws && rest_ws) {
        content.erase(line_start, line_end - line_start + (line_end < content.size() ? 1 : 0));
    } else {
        // Inline: remove " rel_path" (prefer eating the leading space)
        size_t sp = content.rfind(' ', pos);
        if (sp != std::string::npos && sp == pos - 1)
            content.erase(sp, rel_path.size() + 1);
        else
            content.erase(pos, rel_path.size());
    }

    std::ofstream fout(cmake_path);
    if (!fout) return false;
    fout << content;
    return true;
}

bool GediProject::removeFileFromMakefile(const std::string &project_root, const std::string &rel_path)
{
    std::string makefile_path = (std::filesystem::path(project_root) / "Makefile").string();
    std::ifstream fin(makefile_path);
    if (!fin) return false;
    std::string content((std::istreambuf_iterator<char>(fin)), {});
    fin.close();

    // Try " rel_path" (with leading space, most common)
    size_t pos = content.find(" " + rel_path);
    if (pos != std::string::npos) {
        content.erase(pos, rel_path.size() + 1);
    } else {
        pos = content.find(rel_path);
        if (pos == std::string::npos) return false;
        content.erase(pos, rel_path.size());
    }

    std::ofstream fout(makefile_path);
    if (!fout) return false;
    fout << content;
    return true;
}

bool GediProject::removeFileFromMesonBuild(const std::string &project_root, const std::string &rel_path)
{
    std::string meson_path = (std::filesystem::path(project_root) / "meson.build").string();
    std::ifstream fin(meson_path);
    if (!fin) return false;
    std::string content((std::istreambuf_iterator<char>(fin)), {});
    fin.close();

    // Try ", 'rel_path'" (not the first entry)
    std::string p1 = ", '" + rel_path + "'";
    size_t pos = content.find(p1);
    if (pos != std::string::npos) {
        content.erase(pos, p1.size());
    } else {
        // Try "'rel_path', " (first entry)
        std::string p2 = "'" + rel_path + "', ";
        pos = content.find(p2);
        if (pos != std::string::npos) {
            content.erase(pos, p2.size());
        } else {
            // Last entry alone
            std::string p3 = "'" + rel_path + "'";
            pos = content.find(p3);
            if (pos == std::string::npos) return false;
            content.erase(pos, p3.size());
        }
    }

    std::ofstream fout(meson_path);
    if (!fout) return false;
    fout << content;
    return true;
}

bool GediProject::addFileToMakefile(const std::string &project_root, const std::string &rel_path)
{
    std::string makefile_path = (std::filesystem::path(project_root) / "Makefile").string();
    std::ifstream fin(makefile_path);
    if (!fin) return false;
    std::string content((std::istreambuf_iterator<char>(fin)), {});
    fin.close();

    // Locate the SRCS line
    size_t pos = content.find("SRCS");
    if (pos == std::string::npos) return false;
    size_t line_end = content.find('\n', pos);
    if (line_end == std::string::npos) line_end = content.size();

    // Already listed?
    if (content.substr(pos, line_end - pos).find(rel_path) != std::string::npos)
        return true;

    content.insert(line_end, " " + rel_path);

    std::ofstream fout(makefile_path);
    if (!fout) return false;
    fout << content;
    return true;
}

bool GediProject::addFileToMesonBuild(const std::string &project_root, const std::string &rel_path)
{
    std::string meson_path = (std::filesystem::path(project_root) / "meson.build").string();
    std::ifstream fin(meson_path);
    if (!fin) return false;
    std::string content((std::istreambuf_iterator<char>(fin)), {});
    fin.close();

    // Find the opening [ of the sources list
    size_t pos = content.find("sources:");
    if (pos == std::string::npos) return false;
    size_t open_br  = content.find('[', pos);
    size_t close_br = content.find(']', open_br);
    if (open_br == std::string::npos || close_br == std::string::npos) return false;

    std::string sources_section = content.substr(open_br, close_br - open_br);
    if (sources_section.find("'" + rel_path + "'") != std::string::npos ||
        sources_section.find("\"" + rel_path + "\"") != std::string::npos)
        return true;   // already listed

    content.insert(close_br, ", '" + rel_path + "'");

    std::ofstream fout(meson_path);
    if (!fout) return false;
    fout << content;
    return true;
}

bool GediProject::addFileToCMakeLists(const std::string &cmake_path, const std::string &new_file)
{
    namespace fs = std::filesystem;

    std::ifstream fin(cmake_path);
    if (!fin) return false;
    std::string content((std::istreambuf_iterator<char>(fin)), {});
    fin.close();

    // Compute relative path from cmake dir to the new file
    std::string rel;
    try { rel = fs::relative(new_file, fs::path(cmake_path).parent_path()).string(); }
    catch (...) { rel = new_file; }

    for (const char* kw : {"add_executable(", "add_library("}) {
        size_t pos = content.find(kw);
        if (pos == std::string::npos) continue;

        // Find the matching closing ')' using depth tracking
        int depth = 0;
        size_t close = std::string::npos;
        for (size_t i = pos; i < content.size(); ++i) {
            if      (content[i] == '(') ++depth;
            else if (content[i] == ')') { if (--depth == 0) { close = i; break; } }
        }
        if (close == std::string::npos) continue;

        // Already listed between the call and its closing ')'?
        if (content.find(rel, pos) < close) return true;

        // Decide how to insert: if ')' is on its own line, indent to match; else inline.
        size_t line_start = content.rfind('\n', close);
        std::string ins;
        if (line_start != std::string::npos) {
            std::string before = content.substr(line_start + 1, close - line_start - 1);
            bool only_ws = before.find_first_not_of(" \t") == std::string::npos;
            if (only_ws && !before.empty()) {
                ins = before + rel + "\n";   // same indent as ')'
            } else {
                ins = " " + rel;             // all on one line → just append
            }
        } else {
            ins = " " + rel;
        }
        content.insert(close, ins);

        std::ofstream fout(cmake_path);
        if (!fout) return false;
        fout << content;
        return true;
    }
    return false;
}

ProjectTarget::ProjectTarget(const std::string &name, const std::vector<std::string> &sources) : name(name), sources(sources)
{
}

ProjectTarget::ProjectTarget(const std::string &name) : ProjectTarget(name, {}) {}

bool ProjectTarget::isExecutable() const
{
    return type == TYPE_KEYS[0];
}

bool ProjectTarget::isSharedLibrary() const
{
    return type == TYPE_KEYS[2];
}

bool ProjectTarget::isStaticLibrary() const
{
    return type == TYPE_KEYS[1];
}

std::string ProjectTarget::abbr() const
{
    return (type == "executable") ? "exe" : (type == "static_library") ? "lib" : "dll";
}
