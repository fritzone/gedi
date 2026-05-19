#include "GediProject.h"

#include <filesystem>
#include <fstream>
#include <string>

// ── helpers ───────────────────────────────────────────────────────────────────

static std::string trim(const std::string& s)
{
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Split "key: value" → ("key", "value").  If no colon, returns ("", line).
static std::pair<std::string, std::string> splitKV(const std::string& line)
{
    const auto colon = line.find(':');
    if (colon == std::string::npos) return {{}, trim(line)};
    return {trim(line.substr(0, colon)), trim(line.substr(colon + 1))};
}

// ── GediProject ───────────────────────────────────────────────────────────────

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
    f << "cpp_standard: " << cpp_standard << "\n";

    if (!sources.empty()) {
        f << "\n[sources]\n";
        for (const auto& s : sources)
            f << s << "\n";
    }

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

    enum class Section { Header, Sources, Library } section = Section::Header;
    LibraryInfo current_lib;
    bool        in_lib = false;

    auto flush_lib = [&]() {
        if (in_lib && !current_lib.short_name.empty())
            proj.libraries.push_back(std::move(current_lib));
        current_lib = {};
        in_lib = false;
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
                flush_lib();
                section = Section::Sources;
            } else if (sec == "library") {
                flush_lib();
                section  = Section::Library;
                in_lib   = true;
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
            else if (key == "cpp_standard") proj.cpp_standard = val;
        } else if (section == Section::Library) {
            if      (key == "short_name")              current_lib.short_name              = val;
            else if (key == "version")                 current_lib.version                 = val;
            else if (key == "cmake_find_package_hint") current_lib.cmake_find_package_hint = val;
            else if (key == "include_dir")             current_lib.include_directories.push_back(val);
            else if (key == "link_lib")                current_lib.link_libraries.push_back(val);
            else if (key == "link_dir")                current_lib.link_directories.push_back(val);
            else if (key == "compiler_flag")           current_lib.compiler_flags.push_back(val);
        }
    }

    flush_lib();
    out = std::move(proj);
    return true;
}
