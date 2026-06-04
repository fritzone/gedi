#include "DependencyChecker.h"
#include "nlohmann/json.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string which(const std::string& name)
{
    std::string cmd = "which " + name + " 2>/dev/null";
    char buf[512];
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return {};
    std::string result;
    if (fgets(buf, sizeof(buf), p))
        result = buf;
    pclose(p);
    if (!result.empty() && result.back() == '\n')
        result.pop_back();
    return result;
}

// Read a single key from /etc/os-release (unquoted value).
static std::string osReleaseField(const std::string& key)
{
    std::ifstream f("/etc/os-release");
    if (!f) return {};
    std::string line;
    const std::string prefix = key + "=";
    while (std::getline(f, line)) {
        if (line.rfind(prefix, 0) == 0) {
            std::string val = line.substr(prefix.size());
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                val = val.substr(1, val.size() - 2);
            return val;
        }
    }
    return {};
}

// Detect the package manager / distro family.
enum class Distro { Debian, Fedora, Arch, Suse, Alpine, Unknown };

static Distro detectDistro()
{
    std::string id   = osReleaseField("ID");
    std::string like = osReleaseField("ID_LIKE");

    auto contains = [](const std::string& haystack, const std::string& needle) {
        return haystack.find(needle) != std::string::npos;
    };

    for (const auto* s : {&id, &like}) {
        if (contains(*s, "debian") || contains(*s, "ubuntu") || contains(*s, "mint"))
            return Distro::Debian;
        if (contains(*s, "fedora") || contains(*s, "rhel") || contains(*s, "centos") || contains(*s, "rocky") || contains(*s, "alma"))
            return Distro::Fedora;
        if (contains(*s, "arch") || contains(*s, "manjaro") || contains(*s, "endeavour"))
            return Distro::Arch;
        if (contains(*s, "opensuse") || contains(*s, "suse"))
            return Distro::Suse;
        if (contains(*s, "alpine"))
            return Distro::Alpine;
    }
    return Distro::Unknown;
}

static void printInstallInstructions(const std::vector<std::string>& missing)
{
    fprintf(stderr, "\nThe following tools are required but were not found:\n");
    for (const auto& m : missing)
        fprintf(stderr, "  - %s\n", m.c_str());
    fprintf(stderr, "\nPlease install them using your package manager:\n\n");

    Distro d = detectDistro();
    switch (d) {
        case Distro::Debian:
            fprintf(stderr, "  sudo apt update\n");
            fprintf(stderr, "  sudo apt install build-essential pkg-config python3\n");
            break;
        case Distro::Fedora:
            fprintf(stderr, "  sudo dnf install gcc gcc-c++ pkgconf-pkg-config python3\n");
            break;
        case Distro::Arch:
            fprintf(stderr, "  sudo pacman -S base-devel pkgconf python\n");
            break;
        case Distro::Suse:
            fprintf(stderr, "  sudo zypper install gcc gcc-c++ pkg-config python3\n");
            break;
        case Distro::Alpine:
            fprintf(stderr, "  sudo apk add build-base pkgconf python3\n");
            break;
        default:
            fprintf(stderr, "  Install a C/C++ compiler (gcc or clang), pkg-config, and python3\n");
            fprintf(stderr, "  using your distribution's package manager.\n");
            break;
    }
    fprintf(stderr, "\nAfter installing, run gedi again.\n");
    fprintf(stderr, "To start gedi anyway and skip this check permanently, run:\n");
    fprintf(stderr, "  gedi --ignore-dependencies\n\n");
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

std::string DependencyChecker::toolchainPath()
{
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";
    return std::string(home) + "/.config/gedi/toolchain.json";
}

bool DependencyChecker::check(Toolchain& out, bool ignore)
{
    std::string path = toolchainPath();

    // --ignore-dependencies: persist the opt-out and skip all further checks.
    if (ignore) {
        try {
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
            json j;
            j["ignore_dependencies"] = true;
            std::ofstream o(path);
            if (o.is_open()) o << j.dump(4) << "\n";
        } catch (...) {}
        return true;
    }

    // If the toolchain file already exists, load it and trust it.
    if (std::filesystem::exists(path)) {
        try {
            std::ifstream f(path);
            json j = json::parse(f);

            // Honour a previously recorded opt-out.
            if (j.value("ignore_dependencies", false))
                return true;

            out.cc         = j.value("cc",         "");
            out.cxx        = j.value("cxx",        "");
            out.clang      = j.value("clang",      "");
            out.clang_cxx  = j.value("clang_cxx",  "");
            out.pkg_config = j.value("pkg_config", "");
            out.python3    = j.value("python3",    "");

            // Sanity: at minimum we need a C++ compiler and python3.
            if (!out.cxx.empty() && !out.python3.empty())
                return true;
            // Fall through to re-probe if the file looks incomplete.
        } catch (...) {
            // Corrupted file — re-probe.
        }
    }

    // First run (or incomplete toolchain file): probe the system.
    Toolchain t;

    // C compiler: prefer gcc, fall back to cc
    t.cc = which("gcc");
    if (t.cc.empty()) t.cc = which("cc");

    // C++ compiler: prefer g++, fall back to c++
    t.cxx = which("g++");
    if (t.cxx.empty()) t.cxx = which("c++");

    // Clang (optional but recorded)
    t.clang     = which("clang");
    t.clang_cxx = which("clang++");

    t.pkg_config = which("pkg-config");

    // Python: prefer python3, fall back to python
    t.python3 = which("python3");
    if (t.python3.empty()) t.python3 = which("python");

    // Determine what is missing.  A C++ compiler and python3 are required;
    // everything else is strongly recommended.
    std::vector<std::string> missing;
    if (t.cxx.empty())
        missing.push_back("c++ compiler (g++ or clang++)");
    if (t.cc.empty() && t.clang.empty())
        missing.push_back("c compiler (gcc or clang)");
    if (t.pkg_config.empty())
        missing.push_back("pkg-config");
    if (t.python3.empty())
        missing.push_back("python3");

    if (!missing.empty()) {
        fprintf(stderr,
            "\n"
            "========================================================\n"
            " gedi cannot start: required build tools are missing\n"
            "========================================================\n");
        printInstallInstructions(missing);
        return false;
    }

    // All good — persist to ~/.config/gedi/toolchain.json.
    try {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        json j;
        j["cc"]         = t.cc;
        j["cxx"]        = t.cxx;
        j["clang"]      = t.clang;
        j["clang_cxx"]  = t.clang_cxx;
        j["pkg_config"] = t.pkg_config;
        j["python3"]    = t.python3;

        std::ofstream o(path);
        if (o.is_open())
            o << j.dump(4) << "\n";
    } catch (...) {
        // Non-fatal: if we can't write the file, just continue.
    }

    out = t;
    return true;
}
