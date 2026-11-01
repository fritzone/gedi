#include "ToolchainDetect.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
// windows.h defines max/min as macros, which break every std::max below. The
// editor target does not pass NOMINMAX, so this file has to say it itself.
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace toolchain {
namespace {

bool isDir(const fs::path& p)  { std::error_code ec; return fs::is_directory(p, ec); }
bool isFile(const fs::path& p) { std::error_code ec; return fs::is_regular_file(p, ec); }

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// INCLUDE, LIB and PATH are all semicolon-separated on Windows.
std::string joinSemi(const std::vector<std::string>& parts) {
    std::string s;
    for (const auto& p : parts) {
        if (p.empty()) continue;
        if (!s.empty()) s += ';';
        s += p;
    }
    return s;
}

// Append `dir` to `out` when it actually exists. Handing a compiler an INCLUDE
// full of directories that are not there is how a "configured" toolchain ends
// up failing on the first #include.
void addIfDir(std::vector<std::string>& out, const fs::path& dir) {
    if (isDir(dir)) out.push_back(dir.string());
}

// A dotted version, compared numerically: "14.9" must not beat "14.41".
std::vector<long> versionParts(const std::string& v) {
    std::vector<long> out;
    std::stringstream ss(v);
    std::string part;
    while (std::getline(ss, part, '.')) {
        try { out.push_back(std::stol(part)); } catch (...) { out.push_back(0); }
    }
    return out;
}

bool versionLess(const std::string& a, const std::string& b) {
    const std::vector<long> va = versionParts(a), vb = versionParts(b);
    for (size_t i = 0; i < std::max(va.size(), vb.size()); ++i) {
        const long x = i < va.size() ? va[i] : 0;
        const long y = i < vb.size() ? vb[i] : 0;
        if (x != y) return x < y;
    }
    return false;
}

// The highest-numbered subdirectory of `parent`, by version order. Used for both
// the MSVC toolset directories and the Windows SDK versions, which are laid out
// the same way.
std::string newestVersionDir(const fs::path& parent) {
    std::error_code ec;
    if (!isDir(parent)) return {};
    std::string best;
    for (const auto& e : fs::directory_iterator(parent, ec)) {
        if (ec) break;
        if (!e.is_directory(ec)) continue;
        const std::string name = e.path().filename().string();
        if (name.empty() || !std::isdigit((unsigned char)name[0])) continue;
        if (best.empty() || versionLess(best, name)) best = name;
    }
    return best;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// Small path/string helpers. Deliberately local: this module is shared between
// the editor and the setup stub, so it must not reach into either one's own
// utilities.
std::string nativePath(const std::string& p) {
    std::string s = p;
#ifdef _WIN32
    std::replace(s.begin(), s.end(), '/', '\\');
    while (s.size() > 3 && s.back() == '\\') s.pop_back();
#endif
    return s;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return nativePath(b);
    if (b.empty()) return nativePath(a);
    std::string s = a;
    if (s.back() != '\\' && s.back() != '/') s += '\\';
    return nativePath(s + b);
}

// Expand %VARIABLES% the way the shell would, so a path the user typed with one
// in it resolves.
std::string expandEnv(const std::string& s) {
#ifdef _WIN32
    if (s.find('%') == std::string::npos) return s;
    const DWORD n = ExpandEnvironmentStringsA(s.c_str(), nullptr, 0);
    if (n == 0) return s;
    std::string out(n, '\0');
    if (ExpandEnvironmentStringsA(s.c_str(), out.data(), n) == 0) return s;
    out.resize(n > 0 ? n - 1 : 0);          // drop the terminator
    return out;
#else
    return s;
#endif
}

#ifdef _WIN32

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                      nullptr, 0, nullptr, nullptr);
    std::string s((size_t)std::max(n, 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                   s.data(), n, nullptr, nullptr);
    return s;
}

// Run a program and return everything it wrote to stdout/stderr.
//
// CreateProcess rather than popen: both callers are Windows-subsystem processes
// (setup, and the SDL build of the editor), so going through cmd.exe would both
// flash a console on screen and subject the command line to cmd's own
// quote-stripping rule. Child stdin is the null device, so a tool that
// unexpectedly asks something cannot wedge the caller.
std::string runCapture(const std::string& exe, const std::string& args) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return {};
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);   // our end stays private

    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError  = wr;
    si.hStdInput  = (nul == INVALID_HANDLE_VALUE) ? nullptr : nul;

    std::string cmdline = "\"" + exe + "\"";
    if (!args.empty()) cmdline += " " + args;
    std::vector<char> buf(cmdline.begin(), cmdline.end());
    buf.push_back('\0');

    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessA(nullptr, buf.data(), nullptr, nullptr,
                                        /*bInheritHandles=*/TRUE, CREATE_NO_WINDOW,
                                        nullptr, nullptr, &si, &pi);
    CloseHandle(wr);        // the child must hold the only writer, or the read below never ends
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) { CloseHandle(rd); return {}; }

    std::string out;
    char chunk[4096];
    DWORD n = 0;
    while (ReadFile(rd, chunk, sizeof chunk, &n, nullptr) && n > 0) out.append(chunk, n);
    CloseHandle(rd);

    WaitForSingleObject(pi.hProcess, 20000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return out;
}

// A REG_SZ value, or "" when it is absent. Checked through both registry views:
// the Windows Kits key is written by a 32-bit installer, so a 64-bit process
// only sees it under the WOW6432 view.
std::string regString(HKEY root, const wchar_t* subkey, const wchar_t* name) {
    for (REGSAM view : { (REGSAM)KEY_WOW64_32KEY, (REGSAM)KEY_WOW64_64KEY }) {
        HKEY k = nullptr;
        if (RegOpenKeyExW(root, subkey, 0, KEY_READ | view, &k) != ERROR_SUCCESS) continue;
        DWORD type = 0, bytes = 0;
        if (RegQueryValueExW(k, name, nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= sizeof(wchar_t)) {
            std::wstring w(bytes / sizeof(wchar_t), L'\0');
            if (RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)w.data(), &bytes) == ERROR_SUCCESS) {
                while (!w.empty() && w.back() == L'\0') w.pop_back();   // REG_SZ keeps its NUL
                RegCloseKey(k);
                return toUtf8(w);
            }
        }
        RegCloseKey(k);
    }
    return {};
}

// Every Visual Studio installation that carries the C++ tools, newest first.
std::vector<std::string> vsInstallPaths() {
    std::vector<std::string> out;

    // vswhere is the supported way to ask, and it knows about installations put
    // somewhere other than the default directory. It ships with VS 2017 and up.
    for (const char* var : { "ProgramFiles(x86)", "ProgramFiles" }) {
        const std::string pf = env(var);
        if (pf.empty()) continue;
        const fs::path vw = fs::path(pf) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
        if (!isFile(vw)) continue;

        const std::string text = runCapture(vw.string(),
            "-products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 "
            "-sort -property installationPath");
        std::stringstream ss(text);
        std::string line;
        while (std::getline(ss, line)) {
            const std::string p = trim(line);
            if (!p.empty() && isDir(fs::path(p) / "VC" / "Tools" / "MSVC")) out.push_back(p);
        }
        if (!out.empty()) return out;       // -sort already puts the newest first
        break;
    }

    // No vswhere, or it reported nothing: look where the installer puts things,
    // "<Program Files>/Microsoft Visual Studio/<year>/<edition>".
    for (const char* var : { "ProgramFiles", "ProgramFiles(x86)" }) {
        const std::string pf = env(var);
        if (pf.empty()) continue;
        const fs::path root = fs::path(pf) / "Microsoft Visual Studio";
        if (!isDir(root)) continue;
        std::error_code ec;
        for (const auto& year : fs::directory_iterator(root, ec)) {
            if (ec) break;
            if (!year.is_directory(ec)) continue;
            for (const auto& ed : fs::directory_iterator(year.path(), ec)) {
                if (ec) break;
                if (ed.is_directory(ec) && isDir(ed.path() / "VC" / "Tools" / "MSVC"))
                    out.push_back(ed.path().string());
            }
        }
    }
    std::sort(out.begin(), out.end(), std::greater<std::string>());   // newest year first
    return out;
}

// The Windows SDK root and the newest version in it that has the UCRT headers.
bool windowsSdk(fs::path& root, std::string& version) {
    std::string kits = regString(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", L"KitsRoot10");
    if (kits.empty()) {
        const std::string pf = env("ProgramFiles(x86)");
        if (!pf.empty() && isDir(fs::path(pf) / "Windows Kits" / "10"))
            kits = (fs::path(pf) / "Windows Kits" / "10").string();
    }
    if (kits.empty()) return false;

    root = fs::path(kits);
    // The newest version directory that is actually populated: an SDK can be
    // half-removed and leave an empty folder behind.
    std::error_code ec;
    std::string best;
    for (const auto& e : fs::directory_iterator(root / "Include", ec)) {
        if (ec) break;
        if (!e.is_directory(ec)) continue;
        if (!isFile(e.path() / "ucrt" / "stdio.h")) continue;
        const std::string name = e.path().filename().string();
        if (best.empty() || versionLess(best, name)) best = name;
    }
    version = best;
    return !version.empty();
}

#else   // !_WIN32 - the installer is Windows-only; these keep it compiling elsewhere

std::string runCapture(const std::string&, const std::string&) { return {}; }
std::vector<std::string> vsInstallPaths() { return {}; }
bool windowsSdk(fs::path&, std::string&) { return false; }

#endif

// The subdirectories of a user-given root where each kind keeps its compilers.
// Open Watcom is the odd one out: no bin\, but binnt64\ and binnt\.
const std::vector<std::string>& binSubdirs(Kind k) {
    static const std::vector<std::string> generic = { "", "bin" };
    static const std::vector<std::string> watcom  = { "", "binnt64", "binnt", "bin" };
    return k == Kind::Watcom ? watcom : generic;
}

// Resolve what the user typed to the compiler executable itself.
fs::path resolveExe(Kind k, const std::string& given, std::string& error) {
    if (isFile(given)) return fs::path(given);
    if (!isDir(given)) {
        error = given + " does not exist.";
        return {};
    }
    for (const auto& sub : binSubdirs(k)) {
        const fs::path dir = sub.empty() ? fs::path(given) : fs::path(given) / sub;
        const fs::path c   = dir / exeName(k);
        if (isFile(c)) return c;
    }
    error = std::string(exeName(k)) + " was not found in " + given + " or its bin directory.";
    return {};
}

// The first non-empty line of "<exe> --version", which is how a compiler
// identifies itself. Turns "that file exists" into "that really is a compiler",
// and gives the user something recognisable to confirm against.
std::string versionLine(const fs::path& exe) {
    const std::string text = runCapture(exe.string(), "--version");
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        std::string t = trim(line);
        if (t.empty()) continue;
        // Clang appends its git URL and revision, which is most of the line and
        // none of what the user needs to recognise it by.
        const size_t url = t.find(" (http");
        if (url != std::string::npos) t = t.substr(0, url);
        if (t.size() > 58) t = t.substr(0, 55) + "...";
        return t;
    }
    return {};
}

// The user may legitimately point at a differently-named driver - MinGW's
// x86_64-w64-mingw32-g++.exe, say - so an unexpected name is worth saying out
// loud rather than refusing.
void noteIfUnexpectedName(Setup& s, Kind k, const fs::path& exe) {
    std::string name = exe.filename().string();
    std::string want = exeName(k);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    std::transform(want.begin(), want.end(), want.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (name.find(want.substr(0, want.find('.'))) == std::string::npos)
        s.notes.push_back("Note: " + exe.filename().string() + " is not the expected " +
                          exeName(k) + ".");
}

// A clang that carries its own headers and libraries - llvm-mingw, MSYS2 - as
// opposed to the stock LLVM build for Windows, which targets MSVC and has
// nothing of its own to compile against.
bool clangIsSelfContained(const fs::path& root) {
    return isDir(root / "include" / "c++") ||
           isDir(root / "x86_64-w64-mingw32") ||
           isDir(root / "lib" / "clang");
}

Setup setupClang(const fs::path& exe) {
    Setup s;
    s.kind = Kind::Clang;
    const fs::path bin  = exe.parent_path();
    const fs::path root = bin.parent_path();

    s.cxx = exe.string();
    s.cc  = isFile(bin / "clang.exe") ? (bin / "clang.exe").string() : exe.string();
    s.bin_dirs.push_back(bin.string());

    const std::string ver = versionLine(exe);
    s.notes.push_back(ver.empty() ? ("Clang at " + bin.string()) : ver);
    if (ver.empty())
        s.notes.push_back("Warning: it did not answer --version; is this really a compiler?");
    noteIfUnexpectedName(s, Kind::Clang, exe);

    if (clangIsSelfContained(root)) {
        s.notes.push_back("Self-contained: it brings its own headers and libraries.");
        return s;
    }

    // The stock LLVM installer's clang compiles against the MSVC headers and
    // links against the MSVC libraries, so without them it cannot build a
    // "Hello, world". Borrow them from Visual Studio when it is there.
    std::string err;
    const Setup msvc = detectMsvc(err);
    if (msvc.ok()) {
        s.env = msvc.env;
        for (const auto& d : msvc.bin_dirs) s.bin_dirs.push_back(d);
        s.notes.push_back("Targets MSVC; using the Visual Studio headers and libraries.");
    } else {
        s.notes.push_back("Warning: no bundled headers, and no Visual Studio to borrow them from.");
    }
    return s;
}

Setup setupGcc(const fs::path& exe) {
    Setup s;
    s.kind = Kind::Gcc;
    const fs::path bin = exe.parent_path();

    s.cxx = exe.string();
    s.cc  = isFile(bin / "gcc.exe") ? (bin / "gcc.exe").string() : exe.string();
    // GCC finds its own headers and libraries relative to where it is installed,
    // so there is nothing to set beyond putting its bin directory on PATH - for
    // the assembler and linker it drives, its runtime DLLs, and mingw32-make.
    s.bin_dirs.push_back(bin.string());

    const std::string ver = versionLine(exe);
    s.notes.push_back(ver.empty() ? ("GCC at " + bin.string()) : ver);
    if (ver.empty())
        s.notes.push_back("Warning: it did not answer --version; is this really a compiler?");
    noteIfUnexpectedName(s, Kind::Gcc, exe);
    if (!isDir(bin.parent_path() / "include"))
        s.notes.push_back("Warning: no include directory next to it; this may be an incomplete install.");
    return s;
}

Setup setupWatcom(const fs::path& exe) {
    Setup s;
    s.kind = Kind::Watcom;
    const fs::path bin  = exe.parent_path();
    const fs::path root = bin.parent_path();

    // owcc is Open Watcom's gcc-compatible driver. It is the one that
    // understands "-o" and "-I", so prefer it over wpp386 when it is installed.
    const fs::path owcc = bin / "owcc.exe";
    if (isFile(owcc)) {
        s.cxx = owcc.string();
        s.cc  = owcc.string();
        s.notes.push_back("Using owcc, Open Watcom's gcc-style driver.");
    } else {
        s.cxx = exe.string();
        s.cc  = isFile(bin / "wcc386.exe") ? (bin / "wcc386.exe").string() : exe.string();
    }

    // The Watcom tools read all of this from the environment and simply fail
    // without it - WATCOM to find themselves, INCLUDE for the headers.
    s.env["WATCOM"] = root.string();

    std::vector<std::string> inc;
    addIfDir(inc, root / "h");
    addIfDir(inc, root / "h" / "nt");
    addIfDir(inc, root / "h" / "nt" / "directx");
    addIfDir(inc, root / "h" / "nt" / "ddk");
    s.env["INCLUDE"] = joinSemi(inc);

    if (isDir(root / "eddat")) s.env["EDPATH"] = (root / "eddat").string();
    if (isDir(root / "wipfc")) s.env["WIPFC"]  = (root / "wipfc").string();

    // 64-bit host tools first, 32-bit as the fallback - an older install only
    // has binnt.
    addIfDir(s.bin_dirs, root / "binnt64");
    addIfDir(s.bin_dirs, root / "binnt");
    if (s.bin_dirs.empty()) s.bin_dirs.push_back(bin.string());

    s.notes.push_back("Open Watcom at " + root.string());
    if (inc.empty())
        s.notes.push_back("Warning: no h\\ directory - this does not look like an Open Watcom root.");
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
//  Names
// ---------------------------------------------------------------------------
const char* id(Kind k) {
    switch (k) {
        case Kind::Msvc:   return "msvc";
        case Kind::Clang:  return "clang";
        case Kind::Gcc:    return "gcc";
        case Kind::Watcom: return "watcom";
        case Kind::None:   break;
    }
    return "none";
}

Kind kindFromId(const std::string& s) {
    for (Kind k : kinds())
        if (s == id(k) && k != Kind::None) return k;
    return Kind::None;
}

std::string versionOf(const std::string& exe) {
    if (exe.empty() || !isFile(exe)) return {};
    return versionLine(fs::path(exe));
}

const char* label(Kind k) {
    switch (k) {
        case Kind::Msvc:   return "Microsoft Visual C++ (MSVC)";
        case Kind::Clang:  return "Clang / LLVM";
        case Kind::Gcc:    return "GCC (MinGW-w64)";
        case Kind::Watcom: return "Open Watcom";
        case Kind::None:   break;
    }
    return "Skip - set a compiler up later from the editor";
}

const char* exeName(Kind k) {
    switch (k) {
        case Kind::Msvc:   return "cl.exe";
        case Kind::Clang:  return "clang++.exe";
        case Kind::Gcc:    return "g++.exe";
        case Kind::Watcom: return "wpp386.exe";
        case Kind::None:   break;
    }
    return "";
}

const char* prompt(Kind k) {
    switch (k) {
        case Kind::Clang:  return "Path to clang++.exe (or the LLVM directory):";
        case Kind::Gcc:    return "Path to g++.exe (or the MinGW directory):";
        case Kind::Watcom: return "Path to wpp386.exe (or the Open Watcom directory):";
        case Kind::Msvc:
        case Kind::None:   break;
    }
    return "";
}

const char* browsePattern(Kind k) {
    switch (k) {
        // A cross-built MinGW names its drivers <triple>-g++.exe, and Open
        // Watcom 2.0 ships owcc alongside the classic wpp386.
        case Kind::Clang:  return "clang++.exe;clang.exe;*-clang++.exe";
        case Kind::Gcc:    return "g++.exe;gcc.exe;*-g++.exe;*-gcc.exe";
        case Kind::Watcom: return "wpp386.exe;owcc.exe;wcc386.exe";
        case Kind::Msvc:   return "cl.exe";
        case Kind::None:   break;
    }
    return "*.exe";
}

bool needsPath(Kind k) {
    return k == Kind::Clang || k == Kind::Gcc || k == Kind::Watcom;
}

std::string suggestedDir(Kind k, const std::string& current) {
    // Whatever the user has already typed wins, so Browse reopens where they
    // left off rather than jumping back to a guess.
    if (!current.empty()) {
        const fs::path p = nativePath(expandEnv(trim(current)));
        if (isDir(p))                      return p.string();
        if (isDir(p.parent_path()))        return p.parent_path().string();
    }

    // Otherwise the places these toolchains actually install themselves.
    std::vector<fs::path> guesses;
    const fs::path pf   = env("ProgramFiles").empty()      ? fs::path() : fs::path(env("ProgramFiles"));
    const fs::path pf86 = env("ProgramFiles(x86)").empty() ? fs::path() : fs::path(env("ProgramFiles(x86)"));

    switch (k) {
        case Kind::Clang:
            if (!pf.empty())   guesses.push_back(pf / "LLVM" / "bin");
            if (!pf.empty())   guesses.push_back(pf / "LLVM");
            guesses.push_back("C:\\msys64\\clang64\\bin");
            break;
        case Kind::Gcc:
            guesses.push_back("C:\\msys64\\mingw64\\bin");
            guesses.push_back("C:\\mingw64\\bin");
            guesses.push_back("C:\\MinGW\\bin");
            break;
        case Kind::Watcom:
            guesses.push_back("C:\\WATCOM\\binnt64");
            guesses.push_back("C:\\WATCOM");
            break;
        case Kind::Msvc:
        case Kind::None:
            break;
    }
    for (const auto& g : guesses)
        if (isDir(g)) return g.string();

    if (!pf.empty())   return pf.string();
    if (!pf86.empty()) return pf86.string();
    return {};
}

const std::vector<Kind>& kinds() {
    static const std::vector<Kind> all = {
        Kind::Msvc, Kind::Clang, Kind::Gcc, Kind::Watcom, Kind::None
    };
    return all;
}

// ---------------------------------------------------------------------------
//  Detection
// ---------------------------------------------------------------------------
Setup detectMsvc(std::string& error) {
    error.clear();

    const std::vector<std::string> installs = vsInstallPaths();
    if (installs.empty()) {
        error = "No Visual Studio with the C++ tools was found on this computer. "
                "Install the \"Desktop development with C++\" workload, or choose "
                "another compiler.";
        return {};
    }

    for (const std::string& vs : installs) {
        const fs::path tools_root = fs::path(vs) / "VC" / "Tools" / "MSVC";

        // Visual Studio records which toolset is current; fall back to the
        // newest one present, since that file is missing on some installs.
        std::string ver;
        const fs::path deflt =
            fs::path(vs) / "VC" / "Auxiliary" / "Build" / "Microsoft.VCToolsVersion.default.txt";
        if (isFile(deflt)) {
            std::ifstream f(deflt);
            std::string line;
            std::getline(f, line);
            ver = trim(line);
        }
        if (ver.empty() || !isDir(tools_root / ver)) ver = newestVersionDir(tools_root);
        if (ver.empty()) continue;

        const fs::path tools = tools_root / ver;

        // x64 host, x64 target: the editor is a 64-bit build, and mixing targets
        // is how you get 94 unresolved externals and one easily missed LNK4272.
        const fs::path bin = tools / "bin" / "Hostx64" / "x64";
        if (!isFile(bin / "cl.exe")) continue;

        Setup s;
        s.kind = Kind::Msvc;
        s.cxx  = (bin / "cl.exe").string();
        s.cc   = s.cxx;                       // cl.exe compiles both languages
        s.bin_dirs.push_back(bin.string());   // also where link.exe lives
        s.notes.push_back("Visual C++ " + ver);
        s.notes.push_back(vs);

        std::vector<std::string> inc, lib;
        addIfDir(inc, tools / "include");
        addIfDir(inc, tools / "atlmfc" / "include");
        addIfDir(lib, tools / "lib" / "x64");
        addIfDir(lib, tools / "atlmfc" / "lib" / "x64");

        fs::path sdk;
        std::string sdkver;
        if (windowsSdk(sdk, sdkver)) {
            for (const char* part : { "ucrt", "shared", "um", "winrt", "cppwinrt" })
                addIfDir(inc, sdk / "Include" / sdkver / part);
            for (const char* part : { "ucrt", "um" })
                addIfDir(lib, sdk / "Lib" / sdkver / part / "x64");
            addIfDir(s.bin_dirs, sdk / "bin" / sdkver / "x64");   // rc.exe, mt.exe
            s.notes.push_back("Windows SDK " + sdkver);
        } else {
            s.notes.push_back("Warning: no Windows SDK found - programs will not link.");
        }

        s.env["INCLUDE"] = joinSemi(inc);
        s.env["LIB"]     = joinSemi(lib);
        return s;
    }

    error = "Visual Studio is installed, but its 64-bit C++ compiler (cl.exe) is not. "
            "Add the \"MSVC v143 - VS 2022 C++ x64/x86 build tools\" component, or "
            "choose another compiler.";
    return {};
}

Setup fromPath(Kind k, const std::string& path, std::string& error) {
    error.clear();
    if (!needsPath(k)) { error = "This toolchain is not configured from a path."; return {}; }

    const std::string given = nativePath(expandEnv(trim(path)));
    if (given.empty()) {
        error = std::string("Please give the path to ") + exeName(k) + ".";
        return {};
    }

    const fs::path exe = resolveExe(k, given, error);
    if (exe.empty()) return {};

    switch (k) {
        case Kind::Clang:  return setupClang(exe);
        case Kind::Gcc:    return setupGcc(exe);
        case Kind::Watcom: return setupWatcom(exe);
        default:           break;
    }
    return {};
}

std::string findPython() {
#ifdef _WIN32
    char buf[2048];
    for (const char* name : { "python.exe", "python3.exe" }) {
        const DWORD n = SearchPathA(nullptr, name, nullptr, (DWORD)sizeof buf, buf, nullptr);
        if (n > 0 && n < sizeof buf) return std::string(buf, n);
    }
#endif
    return {};
}

// ---------------------------------------------------------------------------
//  Persisting
// ---------------------------------------------------------------------------
std::string configPath() {
    // Must agree with DependencyChecker::toolchainPath() in the editor.
    std::string home = env("USERPROFILE");
    if (home.empty()) home = env("TEMP");
    if (home.empty()) home = "C:\\Windows\\Temp";
    return joinPath(home, ".config\\gedi\\toolchain.json");
}

bool write(const Setup& s, const std::string& python3, std::string& error) {
    error.clear();
    if (!s.ok()) { error = "no compiler to record"; return false; }

    const std::string path = configPath();
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);

    // Merge rather than overwrite. The file is not only ours: it also carries
    // the python the editor needs, pkg-config on POSIX, and the
    // "ignore_dependencies" opt-out. Changing the compiler must not silently
    // discard any of that.
    json j = json::object();
    {
        std::ifstream in(path);
        if (in) {
            try {
                json existing = json::parse(in);
                if (existing.is_object()) j = std::move(existing);
            } catch (...) { /* unreadable - start clean rather than refuse */ }
        }
    }

    j["cc"]  = s.cc;
    j["cxx"] = s.cxx;
    // clang / clang_cxx are what the editor hands libclang for highlighting and
    // completion, so only a real clang belongs in them.
    j["clang"]      = (s.kind == Kind::Clang) ? s.cc  : std::string();
    j["clang_cxx"]  = (s.kind == Kind::Clang) ? s.cxx : std::string();
    j["kind"]       = id(s.kind);
    j["bin_dirs"]   = s.bin_dirs;
    j["env"]        = s.env;
    // Only overwrite python when the caller has one; the editor passes the one
    // already recorded, setup passes what it found.
    if (!python3.empty() || !j.contains("python3")) j["python3"] = python3;
    if (!j.contains("pkg_config")) j["pkg_config"] = std::string();

    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) { error = "cannot write " + path; return false; }
    o << j.dump(4) << "\n";
    o.flush();
    if (!o) { error = "cannot write " + path; return false; }
    return true;
}

} // namespace toolchain
