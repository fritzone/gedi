#include "DependencyChecker.h"
#include "platform_compat.h"
#include "nlohmann/json.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#ifdef _WIN32
// Directory holding the running executable.
static std::filesystem::path selfDir()
{
    char buf[4096];
    const long n = win_self_exe_path(buf, sizeof buf);
    if (n <= 0) return {};
    return std::filesystem::path(std::string(buf, (size_t)n)).parent_path();
}

// Directory holding the bundled development environment - toolchain\ and
// buildtools\ - or "" when this build was not given one. Shipping them is what
// lets a fresh Windows machine build C++ with no Visual Studio, no MinGW and
// nothing on PATH.
//
// Two layouts resolve here: installed, where they sit next to the executable,
// and the build tree, where the executable is in build\ and they are under the
// sibling thirdparty\ of that directory's parent.
static std::filesystem::path bundledRoot()
{
    const std::filesystem::path exe = selfDir();
    if (exe.empty()) return {};

    std::error_code ec;
    const std::filesystem::path candidates[] = {
        exe,
        exe.parent_path(),
        exe.parent_path() / "thirdparty",
    };
    for (const auto& c : candidates)
        if (std::filesystem::exists(c / "toolchain" / "bin" / "clang++.exe", ec)) return c;
    return {};
}

static std::filesystem::path bundledToolchainRoot()
{
    const std::filesystem::path r = bundledRoot();
    return r.empty() ? r : (r / "toolchain");
}
#endif

void DependencyChecker::useBundledTools()
{
#ifdef _WIN32
    const std::filesystem::path root = bundledRoot();
    if (root.empty()) return;

    // Order matters only in that all of these precede whatever the machine
    // already has: a bundled environment that loses to a half-installed MinGW
    // on PATH would be worse than not shipping one.
    const std::filesystem::path dirs[] = {
        root / "toolchain" / "bin",
        root / "buildtools" / "cmake" / "bin",
        root / "buildtools" / "ninja",
        root / "buildtools" / "meson",
    };

    std::string prefix;
    std::error_code ec;
    for (const auto& d : dirs)
        if (std::filesystem::is_directory(d, ec)) prefix += d.string() + ";";
    if (prefix.empty()) return;

    const char* cur = std::getenv("PATH");
    const std::string value = prefix + (cur ? cur : "");

    // Both copies: SetEnvironmentVariable is what CreateProcess hands to child
    // processes, _putenv_s is what the CRT's own getenv/_popen read. Setting
    // only one of them leaves the other stale.
    SetEnvironmentVariableA("PATH", value.c_str());
    _putenv_s("PATH", value.c_str());
#endif
}

static std::string which(const std::string& name)
{
#ifdef _WIN32
    std::string cmd = "where " + name + " 2>NUL";
#else
    std::string cmd = "which " + name + " 2>/dev/null";
#endif
    char buf[512];
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return {};
    std::string result;
    if (fgets(buf, sizeof(buf), p))
        result = buf;
    pclose(p);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    return result;
}

#ifndef _WIN32
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
#endif // !_WIN32

static void printInstallInstructions(const std::vector<std::string>& missing)
{
    std::string msg = "\nThe following tools are required but were not found:\n";
    for (const auto& m : missing)
        msg += "  - " + m + "\n";
    msg += "\nPlease install them using your package manager:\n\n";

#ifdef _WIN32
    msg += "  Install a C++ compiler: either the \"Desktop development with C++\"\n"
           "  workload of Visual Studio (provides cl.exe), or MinGW-w64 (provides\n"
           "  g++.exe), and make sure it is on PATH.\n"
           "  Install Python 3 from https://python.org and make sure python.exe is on PATH.\n";
#else
    Distro d = detectDistro();
    switch (d) {
        case Distro::Debian:
            msg += "  sudo apt update\n  sudo apt install build-essential pkg-config python3\n";
            break;
        case Distro::Fedora:
            msg += "  sudo dnf install gcc gcc-c++ pkgconf-pkg-config python3\n";
            break;
        case Distro::Arch:
            msg += "  sudo pacman -S base-devel pkgconf python\n";
            break;
        case Distro::Suse:
            msg += "  sudo zypper install gcc gcc-c++ pkg-config python3\n";
            break;
        case Distro::Alpine:
            msg += "  sudo apk add build-base pkgconf python3\n";
            break;
        default:
            msg += "  Install a C/C++ compiler (gcc or clang), pkg-config, and python3\n"
                   "  using your distribution's package manager.\n";
            break;
    }
#endif
    msg += "\nAfter installing, run gedi again.\n"
           "To start gedi anyway and skip this check permanently, run:\n"
           "  gedi --ignore-dependencies\n\n";

    fprintf(stderr, "%s", msg.c_str());

#ifdef _WIN32
    // gedi-gui has no console window, so stderr is otherwise invisible -
    // make sure the user actually sees why the app didn't start.
    MessageBoxA(nullptr, msg.c_str(), "gedi - missing dependencies", MB_OK | MB_ICONERROR);
#endif
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

std::string DependencyChecker::toolchainPath()
{
#ifdef _WIN32
    const char* home = getenv("USERPROFILE");
    if (!home) home = getenv("TEMP");
    if (!home) home = "C:\\Windows\\Temp";
#else
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";
#endif
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

            // Sanity: at minimum we need a C++ compiler and python3, and the
            // recorded compiler has to still be there. The file stores absolute
            // paths, so an uninstalled MinGW - or a toolchain directory that
            // moved - would otherwise be trusted forever and every build would
            // fail with "command not found" instead of re-probing.
            const bool cxx_ok = !out.cxx.empty() &&
                                (out.cxx.find_first_of("/\\") == std::string::npos ||
                                 std::filesystem::exists(out.cxx));
            if (cxx_ok && !out.python3.empty())
                return true;
            // Fall through to re-probe if the file looks incomplete.
        } catch (...) {
            // Corrupted file - re-probe.
        }
    }

    // First run (or incomplete toolchain file): probe the system.
    Toolchain t;

#ifdef _WIN32
    // The bundled Clang wins over anything installed on the machine: it is the
    // toolchain this copy was shipped with, it is complete (drivers, linker,
    // headers, import libraries, runtime DLLs), and it does not depend on which
    // Visual Studio happens to be present. Everything else stays as a fallback
    // for source builds that were not given a toolchain.
    const std::filesystem::path tc = bundledToolchainRoot();
    if (!tc.empty()) {
        t.cxx       = (tc / "bin" / "clang++.exe").string();
        t.cc        = (tc / "bin" / "clang.exe").string();
        t.clang     = t.cc;
        t.clang_cxx = t.cxx;
    }

    // Otherwise prefer MSVC (cl.exe), then a MinGW g++/gcc.
    if (t.cxx.empty()) {
        t.cxx = which("cl");
        t.cc  = t.cxx;
    }
    if (t.cxx.empty()) {
        t.cxx = which("g++");
        t.cc  = which("gcc");
    }

    if (t.clang.empty())     t.clang     = which("clang");
    if (t.clang_cxx.empty()) t.clang_cxx = which("clang++");

    // pkg-config isn't part of the Windows toolchain; leave it unset.
    t.pkg_config.clear();

    // Python: prefer python, fall back to python3
    t.python3 = which("python");
    if (t.python3.empty()) t.python3 = which("python3");

    std::vector<std::string> missing;
    if (t.cxx.empty())
        missing.push_back("C++ compiler (MSVC cl.exe or MinGW g++)");
    if (t.python3.empty())
        missing.push_back("python");
#else
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
#endif

    if (!missing.empty()) {
        fprintf(stderr,
            "\n"
            "========================================================\n"
            " gedi cannot start: required build tools are missing\n"
            "========================================================\n");
        printInstallInstructions(missing);
        return false;
    }

    // All good - persist to ~/.config/gedi/toolchain.json.
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
