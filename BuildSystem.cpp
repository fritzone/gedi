#include "BuildSystem.h"
#include "platform_compat.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <sstream>
#include <fstream>
#include <regex>
#include <algorithm>
#include <chrono>
#include <filesystem>

// The SDL build renders a fixed VGA/CP437 character grid, so it cannot display the
// Unicode glyphs GCC/Clang put in diagnostics (curly quotes, em dashes, the box-
// drawing used for caret underlines, …). Those show up as '?'. In the graphical
// build we (a) run the compiler under a C locale so GCC emits plain ASCII, and
// (b) transliterate any remaining Unicode in the captured output to ASCII. The
// text build keeps the original UTF-8 (a real terminal renders it fine).
#ifdef GEDI_GUI
static std::string asciiForCodepoint(uint32_t cp) {
    switch (cp) {
        case 0x2018: case 0x2019: case 0x201B: return "'";    // ' ' ‛
        case 0x201C: case 0x201D: case 0x201F: return "\"";   // " " ‟
        case 0x2013: case 0x2014: case 0x2015: case 0x2212: return "-";  // – - ― −
        case 0x2026:                            return "...";  // …
        case 0x2192:                            return "->";   // →
        case 0x2190:                            return "<-";   // ←
        case 0x2191:                            return "^";    // ↑
        case 0x2193:                            return "v";    // ↓
        case 0x00A0:                            return " ";    // non-breaking space
        case 0x2022: case 0x00B7:               return "*";    // • ·
        case 0x2500: case 0x2501:               return "-";    //  ━
        case 0x2502: case 0x2503:               return "|";    // │ ┃
        case 0x250C: case 0x2510: case 0x2514: case 0x2518:
        case 0x251C: case 0x2524: case 0x252C: case 0x2534:
        case 0x253C:                            return "+";    // box corners / junctions
        default:                                return "?";
    }
}

static std::string asciifyForGui(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    size_t i = 0, n = in.size();
    while (i < n) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x80) { out += (char)c; ++i; continue; }
        int len; uint32_t cp;
        if      ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else { out += '?'; ++i; continue; }              // invalid lead byte
        if (i + (size_t)len > n) { out += '?'; ++i; continue; }
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            unsigned char cc = (unsigned char)in[i + k];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { out += '?'; ++i; continue; }
        out += asciiForCodepoint(cp);
        i += len;
    }
    return out;
}
#ifdef _WIN32
static const char* kLocalePrefix = "";
#else
static const char* kLocalePrefix = "LC_ALL=C ";
#endif

#else
static inline std::string asciifyForGui(const std::string& in) { return in; }
static const char* kLocalePrefix = "";
#endif

BuildSystem::BuildSystem(const Config& config, std::filesystem::path exe_dir)
    : m_config(config), m_exe_dir(exe_dir)
{
    resolveDefinition();
}

void BuildSystem::setConfig(const Config& config)
{
    m_config = config;
    resolveDefinition();
}

// Which definition describes the configured compiler. Kept as an id rather than
// a pointer: the registry is rebuilt when a compiler is introduced, and that
// moves every definition.
void BuildSystem::resolveDefinition()
{
    const Toolchain& tc = m_config.toolchain;
    m_def_id = compilers::Registry::instance().forToolchain(tc.compiler, tc.kind, tc.cxx).id;
}

const compilers::Definition& BuildSystem::definition() const
{
    auto& reg = compilers::Registry::instance();
    if (const compilers::Definition* d = reg.find(m_def_id)) return *d;
    const Toolchain& tc = m_config.toolchain;
    return reg.forToolchain(tc.compiler, tc.kind, tc.cxx);
}

// One command with an environment variable set for it. "VAR=value cmd" is shell
// syntax that cmd.exe does not understand at all - it treats the assignment as
// the program name - so on Windows this has to become a "set" and a chained
// call instead.
static std::string withEnv(const std::string& var, const std::string& value,
                           const std::string& command)
{
#ifdef _WIN32
    return "set \"" + var + "=" + value + "\" && " + command;
#else
    return var + "=\"" + value + "\" " + command;
#endif
}

// A compiler the definition names as the one CMake must use instead of the
// configured driver ("wcl386" for Open Watcom, whose owcc CMake cannot drive),
// found next to that driver.
static std::string siblingCompiler(const std::string& cxx, const std::string& name)
{
    const std::filesystem::path p(cxx);
    return (p.parent_path() / (name + p.extension().string())).string();
}

// Arguments that pin an external build system to the compiler gedi is configured
// to use. Without them cmake runs its own detection and happily picks up a
// Visual Studio that the bundled Clang was meant to replace, so a project build
// would end up using a different toolchain than single-file compiles do.
std::string BuildSystem::cmakeToolchainArgs() const
{
    const std::string& cxx = m_config.toolchain.cxx;
    if (cxx.empty()) return {};
    const compilers::Definition& def = definition();

    std::string a;
    if (!def.cmake.generator.empty()) a += " -G \"" + def.cmake.generator + "\"";
    if (!def.cmake.compiler.empty()) {
        const std::string c = siblingCompiler(cxx, def.cmake.compiler);
        return a + " \"-DCMAKE_CXX_COMPILER=" + c + "\" \"-DCMAKE_C_COMPILER=" + c + "\"";
    }
    a += " \"-DCMAKE_CXX_COMPILER=" + cxx + "\"";
    if (!m_config.toolchain.cc.empty())
        a += " \"-DCMAKE_C_COMPILER=" + m_config.toolchain.cc + "\"";
    return a;
}

void BuildSystem::toolchainChanged()
{
    std::lock_guard<std::mutex> lk(m_cache_mutex);
    m_compile_command_cache.clear();
    m_sys_include_args.clear();
    m_sys_include_cached = false;
}

bool BuildSystem::isMsvcCompiler(const std::string& compilerPath) {
    std::string p = compilerPath;
    if (p.size() >= 2 && p.front() == '"' && p.back() == '"')
        p = p.substr(1, p.size() - 2);
    std::string stem = std::filesystem::path(p).stem().string();
    for (auto& c : stem) c = (char)std::tolower((unsigned char)c);
    return stem == "cl";
}

// The argument that begins at `pos` on a command line, unquoted. Output paths
// are written quoted (they can contain spaces), so splitting on the next blank
// would cut such a path in half; callers want the bare path, since that is what
// gets handed to filesystem::exists() and to CreateProcess afterwards.
static std::string argumentAt(const std::string& cmd, size_t pos) {
    if (pos >= cmd.size()) return "";
    if (cmd[pos] == '"') {
        size_t end = cmd.find('"', pos + 1);
        if (end == std::string::npos) return cmd.substr(pos + 1);
        return cmd.substr(pos + 1, end - pos - 1);
    }
    size_t end = cmd.find(' ', pos);
    return cmd.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

// `pattern` with every "{key}" replaced by `value`.
static std::string fill(std::string pattern, const char* key, const std::string& value)
{
    const std::string k = std::string("{") + key + "}";
    for (size_t at = pattern.find(k); at != std::string::npos; at = pattern.find(k, at + value.size()))
        pattern.replace(at, k.size(), value);
    return pattern;
}

// The definition's output switch naming `exe`, with the path quoted.
static std::string outputSwitch(const compilers::Definition& def, const std::string& exe)
{
    return fill(def.output, "out", "\"" + exe + "\"");
}

// Compile one file with the configured compiler: what guessCompileCommand falls
// back to when the guess script has nothing to say.
static std::string defaultCompileCommand(const compilers::Definition& def,
                                         const std::string& cxx_in, const std::string& filename_in)
{
    const std::string cxx = cxx_in.empty() ? "g++" : cxx_in;
    // Absolute, so the command means the same thing from any directory - some
    // compilers are run from a scratch directory (see runCompilationProcess).
    std::error_code ec;
    std::string filename = std::filesystem::absolute(filename_in, ec).lexically_normal().string();
    if (ec) filename = filename_in;
    const std::string stem = filename.substr(0, filename.find_last_of('.'));
    // Windows drivers append .exe to an extensionless output name, so the
    // command asks for exactly the file that will exist - otherwise the "run
    // it now" step looks for an executable that isn't there.
    return "\"" + cxx + "\"" + def.argsString() + " \"" + filename + "\" " +
           outputSwitch(def, stem + def.exeExtension());
}

CompilationResult BuildSystem::runCompilationProcess(EditorBuffer& buffer) {
    CompilationResult result;
    result.success = false;
    const compilers::Definition& def = definition();

    std::string base_compile_cmd = guessCompileCommand(buffer.filename);
    if (base_compile_cmd.empty()) {
        result.output_lines.push_back("Failed to find build command for " + buffer.filename);
        return result;
    }

    result.output_lines.push_back("Build command: " + base_compile_cmd);
    result.full_command = get_full_compile_command(base_compile_cmd, buffer.compiler_settings);
    result.output_lines.push_back("");
    result.output_lines.push_back("Compiling...");
    result.output_lines.push_back("> " + result.full_command);

    // The executable is whatever follows the definition's output switch.
    // The output switch follows the sources, so its last occurrence as a word
    // of its own: Digital Mars spells both its optimiser and its output "-o".
    const std::string prefix = def.outputPrefix();
    const size_t out_pos = prefix.empty() ? std::string::npos
                                          : result.full_command.rfind(" " + prefix);
    result.executable_name = (out_pos != std::string::npos)
        ? argumentAt(result.full_command, out_pos + 1 + prefix.size())
        : std::string("a.out");

    // Some compilers (owcc, for one) leave their object files in the current
    // directory, so their definition asks for a scratch directory that is
    // removed afterwards. Every path in the command is absolute, and quoted
    // includes are still looked up next to the file that includes them.
    std::string run_cmd = kLocalePrefix + result.full_command;
    std::filesystem::path scratch;
    if (def.scratch_dir) {
        std::error_code ec;
        scratch = std::filesystem::temp_directory_path(ec) /
                  ("gedi-build-" + std::to_string(
                       std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!ec) std::filesystem::create_directories(scratch, ec);
        if (ec) {
            scratch.clear();
        } else {
#ifdef _WIN32
            run_cmd = "cd /d \"" + scratch.string() + "\" && " + run_cmd;
#else
            run_cmd = "cd \"" + scratch.string() + "\" && " + run_cmd;
#endif
        }
    }

    std::string full_compiler_output_str;
    char buffer_arr[512];
    FILE* compile_pipe = popen((run_cmd + " 2>&1").c_str(), "r");
    if (compile_pipe) {
        while (fgets(buffer_arr, sizeof(buffer_arr), compile_pipe) != NULL)
            full_compiler_output_str += buffer_arr;
    }
    int compile_status = compile_pipe ? pclose(compile_pipe) : -1;
    result.success = (compile_status == 0);

    if (!scratch.empty()) {
        std::error_code ec;
        // Outputs the user asked for that land in the current directory too
        // (Open Watcom's linker map) go next to the executable.
        if (!result.executable_name.empty() && !def.keep_outputs.empty()) {
            const std::filesystem::path exe_dir =
                std::filesystem::path(result.executable_name).parent_path();
            for (const auto& e : std::filesystem::directory_iterator(scratch, ec)) {
                const std::string ext = e.path().extension().string();
                if (std::find(def.keep_outputs.begin(), def.keep_outputs.end(), ext) ==
                    def.keep_outputs.end()) continue;
                std::error_code mv;
                std::filesystem::rename(e.path(), exe_dir / e.path().filename(), mv);
                if (mv) {   // another filesystem: copy instead
                    std::filesystem::copy_file(e.path(), exe_dir / e.path().filename(),
                        std::filesystem::copy_options::overwrite_existing, mv);
                }
            }
        }
        std::filesystem::remove_all(scratch, ec);
    }

    // Add compiler output to output_lines for display (ASCII-only in the GUI build)
    std::istringstream ss(asciifyForGui(full_compiler_output_str));
    std::string line;
    while (std::getline(ss, line))
        result.output_lines.push_back(line);

    return result;
}

std::string BuildSystem::settingsFlags(const CompilerSettings& s) const
{
    return compilers::flagsFor(s, definition(), /*for_cmake=*/false);
}

// Compile a one-line program once per option, each time with only that option
// added, and report the options the compiler refuses. The program is linked
// when the compiler can link it as it is; a cross compiler for a bare-metal
// target usually cannot without target-specific settings, so then it is only
// compiled. A compiler that cannot even compile the program with no options
// gets an error instead: then every option would look unsupported.
bool BuildSystem::testOptions(std::set<std::string>& unsupported, std::string& error,
                              const std::function<void(int, int)>& progress) const
{
    unsupported.clear();
    error.clear();
    const compilers::Definition& def = definition();
    const std::string& cxx = m_config.toolchain.cxx;
    if (cxx.empty()) { error = "No compiler is configured."; return false; }

    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) /
        ("gedi-optiontest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir, ec);
    if (ec) { error = "Cannot create a temporary directory: " + ec.message(); return false; }
    const std::filesystem::path src = dir / "t.cpp";
    { std::ofstream f(src); f << "int main() { return 0; }\n"; }
    const std::string exe = (dir / ("t" + def.exeExtension())).string();
    const std::string obj = (dir / "t.o").string();

    std::string base = "\"" + cxx + "\"" + def.argsString();
    for (const auto& a : def.always) base += " " + a;
    bool link = true;

    auto compiles = [&](const std::string& flag) {
#ifdef _WIN32
        std::string cmd = "cd /d \"" + dir.string() + "\" && " + base;
#else
        std::string cmd = "cd \"" + dir.string() + "\" && " + std::string(kLocalePrefix) + base;
#endif
        if (!flag.empty()) cmd += " " + flag;
        cmd += " \"" + src.string() + "\" ";
        cmd += link ? outputSwitch(def, exe)
                    : def.compile_only + " " + fill(def.object_output, "out", "\"" + obj + "\"");
        cmd += " 2>&1";
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) return false;
        char buf[512];
        while (fgets(buf, sizeof buf, p)) {}
        return pclose(p) == 0;
    };

    // Every flag to try, with the key it is reported under.
    std::vector<std::pair<std::string, std::string>> tests;
    for (const compilers::Option* o : def.allOptions()) {
        if (o->radio()) {
            for (size_t i = 0; i < o->choices.size(); ++i)
                if (!o->choices[i].flag.empty())
                    tests.push_back({ o->id + ":" + std::to_string(i), o->choices[i].flag });
        } else if (!o->flag.empty()) {
            tests.push_back({ o->id, o->flag });
        }
    }

    const int total = (int)tests.size() + 1;
    if (progress) progress(0, total);
    bool ok = compiles("");
    if (!ok) { link = false; ok = compiles(""); }      // try compiling only
    if (!ok) {
        error = "The compiler could not build a one-line program without any options, "
                "so the options cannot be tested.";
    } else {
        for (size_t i = 0; i < tests.size(); ++i) {
            if (progress) progress((int)i + 1, total);
            if (!compiles(tests[i].second)) unsupported.insert(tests[i].first);
        }
    }
    std::filesystem::remove_all(dir, ec);
    if (ok) compilers::saveTestedUnsupported(def, cxx, unsupported);
    return ok;
}

// Imported projects (GediProject::build_file) and Open Watcom projects are built
// with their own makefile exactly as the user would from a shell: no CXX/CXXFLAGS
// overrides, since those would clobber whatever the makefile sets up itself.
static std::string externalMakeCommand(const GediProject& project)
{
    const std::filesystem::path bf = std::filesystem::path(project.root) / project.buildFile();
    const std::string dir  = bf.parent_path().lexically_normal().string();
    const std::string file = bf.filename().string();

    if (project.usesWmake()) {
        // wmake has no -C; -h drops the banner.  Its makefiles expect to run from their directory.
#ifdef _WIN32
        return "cd /d \"" + dir + "\" && wmake -h -f \"" + file + "\"";
#else
        return "cd \"" + dir + "\" && wmake -h -f \"" + file + "\"";
#endif
    }

    std::string cmd = "make -C \"" + dir + "\"";
    if (file != "Makefile" && file != "makefile" && file != "GNUmakefile")
        cmd += " -f \"" + file + "\"";
    return cmd;
}

// The program an imported project produces: its first executable target.
static std::string externalExecutable(const GediProject& project)
{
    for (const auto& t : project.targets) {
        if (!t.isExecutable()) continue;
        std::filesystem::path p = std::filesystem::path(project.root) / t.name;
        if (project.build_system == "watcom" && !project.hasExternalBuildFile()) {
            p += project.watcomPlatform().exe_ext;              // as the generated makefile names it
            return p.lexically_normal().string();
        }
#ifdef _WIN32
        if (p.extension() != ".exe") p += ".exe";
#else
        if (project.usesWmake() && p.extension() != ".exe") p += ".exe";   // wlink names it X.exe
#endif
        return p.lexically_normal().string();
    }
    return "";
}

std::string BuildSystem::buildProjectPreview(const GediProject& project, const CompilerSettings& s) const
{
    if (project.hasExternalBuildFile() || project.usesWmake())
        return externalMakeCommand(project);
    const std::string& root = project.root;
    std::string std_num = s.cpp_standard;
    if (std_num.rfind("c++", 0) == 0) std_num = std_num.substr(3);

    const compilers::Definition& def = definition();
    const std::string& cxx = m_config.toolchain.cxx;
    const std::string bt = compilers::cmakeBuildType(s, def);
    const std::string flags = compilers::flagsFor(s, def, false);
    auto withStd = [&](const std::string& f) {
        std::string x = def.standardFlag(s.cpp_standard);
        if (!f.empty()) x += (x.empty() ? "" : " ") + f;
        return x;
    };

    if (project.build_system == "cmake") {
        if (!def.cmake.supported) return "(CMake cannot build with " + def.name + ")";
        const std::string build_dir = root + "/" + def.cmake.build_dir;
        const std::string cmake_flags = compilers::flagsFor(s, def, true);
        std::string args;
        args += " -DCMAKE_BUILD_TYPE=" + bt;
        args += " -DCMAKE_CXX_STANDARD=" + std_num;
        args += cmakeToolchainArgs();
        if (!cmake_flags.empty()) args += " \"-DCMAKE_CXX_FLAGS=" + cmake_flags + "\"";
        return "cmake -S \"" + root + "\" -B \"" + build_dir + "\"" + args + "\n"
             + "cmake --build \"" + build_dir + "\"";
    }
    if (project.build_system == "make") {
        if (!def.make) return "(Make projects cannot be built with " + def.name + ")";
        return "make -C \"" + root + "\" CXX=\"" + cxx + def.argsString() + "\" CXXFLAGS=\"" + withStd(flags) + "\"";
    }
    if (project.build_system == "meson") {
        if (!def.meson) return "(Meson cannot build with " + def.name + " - use CMake, Make or Watcom)";
        std::string build_dir = root + "/builddir";
        std::string buildtype = (bt == "Release") ? "release" : "debug";
        return "meson setup \"" + build_dir + "\" \"" + root + "\" --buildtype=" + buildtype + "\n"
             + withEnv("CXXFLAGS", withStd(flags), "ninja -C \"" + build_dir + "\"");
    }
    return "(unknown build system: " + project.build_system + ")";
}

// After a successful build, search the build tree for the produced executable
// rather than assuming a fixed layout: CMake's default generator on Windows
// (Visual Studio) puts binaries in a per-config subdirectory (build/Debug/...)
// that can't be predicted up front, and the binary needs a .exe suffix there.
static std::string findBuiltExecutable(const std::string& search_root, const std::string& name) {
#ifdef _WIN32
    std::string target = name + ".exe";
#else
    std::string target = name;
#endif
    std::error_code ec;
    if (!std::filesystem::exists(search_root, ec)) return "";
    auto it  = std::filesystem::recursive_directory_iterator(
        search_root, std::filesystem::directory_options::skip_permission_denied, ec);
    auto end = std::filesystem::recursive_directory_iterator();
    for (; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().filename() == target)
            return it->path().string();
    }
    return "";
}

CompilationResult BuildSystem::runProjectBuild(const GediProject& project) {
    CompilationResult result;
    result.success = false;

    const std::string& root = project.root;
    std::string build_dir;
    std::string build_cmd;

    // Helper: run a shell command, append output to result.output_lines, return success
    auto run_cmd = [&](const std::string& cmd) -> bool {
        result.output_lines.push_back("> " + cmd);
        char buf[512];
        std::string out;
        FILE* p = popen((kLocalePrefix + cmd).c_str(), "r");
        if (!p) {
            result.output_lines.push_back("  [failed to start process]");
            return false;
        }
        while (fgets(buf, sizeof(buf), p))
            out += buf;
        int status = pclose(p);
        std::istringstream ss(asciifyForGui(out));
        std::string ln;
        while (std::getline(ss, ln))
            result.output_lines.push_back(ln);
        return (status == 0);
    };
    auto refuse = [&](const std::string& system, const compilers::Definition& def) {
        result.output_lines.push_back(system + " cannot build with " + def.name + ".");
        result.output_lines.push_back("Switch the project's build system in Project > Properties, "
                                      "or pick another compiler in Options > Compiler Options.");
        result.output_lines.push_back("=== Build failed ===");
        return result;
    };

    result.output_lines.push_back("=== Building project: " + project.name + " ===");

    const CompilerSettings& cs = project.compiler_settings;
    const compilers::Definition& def = definition();

    std::string std_num = cs.cpp_standard;
    if (std_num.rfind("c++", 0) == 0) std_num = std_num.substr(3);

    const std::string bt = compilers::cmakeBuildType(cs, def);
    const std::string extra_flags = compilers::flagsFor(cs, def, false);
    std::string cxxflags = def.standardFlag(cs.cpp_standard);
    if (!extra_flags.empty()) cxxflags += (cxxflags.empty() ? "" : " ") + extra_flags;

    if (project.hasExternalBuildFile() || project.usesWmake()) {
        build_dir = root;
        build_cmd = externalMakeCommand(project) + " 2>&1";
        result.executable_name = externalExecutable(project);

    } else if (project.build_system == "cmake") {
        if (!def.cmake.supported) return refuse("CMake", def);
        // A CMake build directory is tied to its generator and compiler, so a
        // definition with a generator of its own gets a directory of its own.
        if (def.cmake.build_dir == "build") {
            for (const char* candidate : {"build", "cmake-build-debug", "cmake-build-release"}) {
                std::string d = root + "/" + candidate;
                if (std::filesystem::exists(d + "/CMakeCache.txt")) { build_dir = d; break; }
            }
        } else {
            build_dir = root + "/" + def.cmake.build_dir;
        }
        if (build_dir.empty() || !std::filesystem::exists(build_dir + "/CMakeCache.txt")) {
            if (build_dir.empty()) build_dir = root + "/build";
            result.output_lines.push_back("No CMake build directory found - configuring...");
        }
        // Always (re)configure so setting changes are picked up
        const std::string cmake_flags = compilers::flagsFor(cs, def, true);
        std::string cmake_args;
        cmake_args += " -DCMAKE_BUILD_TYPE=" + bt;
        cmake_args += " -DCMAKE_CXX_STANDARD=" + std_num;
        cmake_args += cmakeToolchainArgs();
        if (!cmake_flags.empty())
            cmake_args += " \"-DCMAKE_CXX_FLAGS=" + cmake_flags + "\"";
        if (!run_cmd("cmake -S \"" + root + "\" -B \"" + build_dir + "\"" + cmake_args + " 2>&1")) {
            result.output_lines.push_back("=== CMake configure failed ===");
            return result;
        }
        result.output_lines.push_back("");
        build_cmd = "cmake --build \"" + build_dir + "\" 2>&1";
        result.executable_name = build_dir + "/" + project.name;

    } else if (project.build_system == "make") {
        if (!def.make) return refuse("Make", def);
        build_dir = root;
        build_cmd = "make -C \"" + root + "\" CXX=\"" + m_config.toolchain.cxx + def.argsString() +
                    "\" CXXFLAGS=\"" + cxxflags + "\" 2>&1";
        result.executable_name = root + "/" + project.name;

    } else if (project.build_system == "meson") {
        if (!def.meson) return refuse("Meson", def);
        build_dir = root + "/builddir";
        std::string buildtype = (bt == "Release") ? "release" : "debug";
        if (!std::filesystem::exists(build_dir + "/build.ninja")) {
            result.output_lines.push_back("No Meson build directory found - setting up...");
            // Meson takes its compiler from CXX at setup time; without this it
            // would run its own detection and could pick a different one than
            // the cmake and make paths above use.
            const std::string setup = withEnv("CXX", m_config.toolchain.cxx + def.argsString(),
                "meson setup \"" + build_dir + "\" \"" + root + "\" --buildtype=" + buildtype);
            if (!run_cmd(setup + " 2>&1")) {
                result.output_lines.push_back("=== Meson setup failed ===");
                return result;
            }
            result.output_lines.push_back("");
        } else {
            // Update build type if project already configured
            run_cmd("meson configure \"" + build_dir + "\" --buildtype=" + buildtype + " 2>&1");
        }
        build_cmd = withEnv("CXXFLAGS", cxxflags,
                            "ninja -C \"" + build_dir + "\"") + " 2>&1";
        result.executable_name = build_dir + "/" + project.name;

    } else {
        result.output_lines.push_back("Unknown build system: " + project.build_system);
        return result;
    }

    result.success = run_cmd(build_cmd);
    result.output_lines.push_back("");
    result.output_lines.push_back(result.success ? "=== Build successful ===" : "=== Build failed ===");

    if (result.success && !project.hasExternalBuildFile() && !project.usesWmake()) {
        // Replace the pre-build guess with the actual binary location - the
        // guess doesn't account for per-config output subdirectories (CMake's
        // Visual Studio generator) or the platform's executable suffix.
        std::string found = findBuiltExecutable(build_dir, project.name);
        if (!found.empty()) result.executable_name = found;
    }
    return result;
}

std::vector<CompileMessage> BuildSystem::parseCompilerOutput(const std::string& full_output_str,
                                                              std::vector<std::string>& output_lines_out,
                                                              const std::string& base_dir) {
    std::vector<CompileMessage> messages;
    std::istringstream stream(full_output_str);
    std::string line;

    // Matches: /path/to/file.cpp:10:5: error: message
    std::regex re_diag(R"(([^:]+):(\d+):(\d+):\s+(error|warning|note):\s*(.*))");
    // Open Watcom:  file.cpp(12): Error! E029: col(11) symbol 'y' has not been declared
    std::regex re_watcom(R"(^(.+?)\((\d+)\): (Error|Warning|Note)! \w+: (?:col\((\d+)\) )?(.*))");
    // MSVC:  C:\src\file.cpp(12): error C2065: 'y': undeclared identifier  (or "(12,5)")
    std::regex re_msvc(R"(^\s*(.+?)\((\d+)(?:,(\d+))?\)\s*:\s*(fatal error|error|warning|note)\s+\w+\s*:\s*(.*))");
    // The configured compiler's own format, when its definition gives one.
    const compilers::Definition& def = definition();
    std::regex re_custom;
    bool have_custom = false;
    if (def.diagnostics == "regex" && !def.diag_regex.empty()) {
        try { re_custom = std::regex(def.diag_regex); have_custom = true; }
        catch (const std::regex_error&) {}
    }
    auto group = [](const std::smatch& m, int n) {
        return (n > 0 && n < (int)m.size() && m[n].matched) ? m[n].str() : std::string();
    };

    // Recursive make announces directory changes; relative file names in the
    // compiler output that follows are relative to that directory.
    //   make[1]: Entering directory '/abs/sub'   /   make[1]: Leaving directory '/abs/sub'
    std::regex re_enter(R"(make(?:\[\d+\])?: Entering directory [`'"](.*)['"])");
    std::regex re_leave(R"(make(?:\[\d+\])?: Leaving directory)");
    std::vector<std::string> dir_stack;

    while (std::getline(stream, line)) {
        output_lines_out.push_back(line);
        {
            std::smatch dm;
            if (line.find("make") != std::string::npos) {
                if (std::regex_search(line, dm, re_enter)) dir_stack.push_back(dm[1].str());
                else if (std::regex_search(line, re_leave) && !dir_stack.empty()) dir_stack.pop_back();
            }
        }
        const std::string& cur_dir = dir_stack.empty() ? base_dir : dir_stack.back();
        CompileMessage msg;
        msg.full_text = line;

        std::smatch match;
        std::string raw_file, line_str, col_str, type_str;
        if (have_custom && std::regex_search(line, match, re_custom)) {
            raw_file = group(match, def.diag_file);
            line_str = group(match, def.diag_line);
            col_str  = def.diag_col ? group(match, def.diag_col) : "1";
            if (col_str.empty()) col_str = "1";
            type_str = group(match, def.diag_severity);
            std::transform(type_str.begin(), type_str.end(), type_str.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            // Words ("Error", "warning #2020", "Remark") or IBM's letters:
            // (W)arning, (I)nformational, anything else (S, E, U) an error.
            if (type_str.find("error") != std::string::npos)        type_str = "error";
            else if (type_str.find("warn") != std::string::npos ||
                     type_str == "w")                               type_str = "warning";
            else if (type_str.find("note") != std::string::npos ||
                     type_str.find("info") != std::string::npos ||
                     type_str.find("remark") != std::string::npos ||
                     type_str == "i")                               type_str = "note";
            else                                                     type_str = "error";
        } else if (std::regex_search(line, match, re_diag)) {
            raw_file = match[1].str();
            line_str = match[2].str();
            col_str  = match[3].str();
            type_str = match[4].str();
        } else if (line.find("! ") != std::string::npos && std::regex_search(line, match, re_watcom)) {
            raw_file = match[1].str();
#ifndef _WIN32
            std::replace(raw_file.begin(), raw_file.end(), '\\', '/');
#endif
            line_str = match[2].str();
            col_str  = match[4].matched ? match[4].str() : "1";
            type_str = match[3].str();
            std::transform(type_str.begin(), type_str.end(), type_str.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
        } else if (line.find('(') != std::string::npos && std::regex_search(line, match, re_msvc)) {
            raw_file = match[1].str();
            line_str = match[2].str();
            col_str  = match[3].matched ? match[3].str() : "1";
            type_str = match[4].str();
            if (type_str == "fatal error") type_str = "error";
        }

        if (!raw_file.empty()) {
            // Resolve filename to absolute path
            if (raw_file[0] == '/') {
                msg.filename = raw_file;
            } else if (!cur_dir.empty()) {
                std::string candidate = cur_dir + "/" + raw_file;
                std::error_code ec;
                auto abs = std::filesystem::weakly_canonical(candidate, ec);
                if (!ec && std::filesystem::exists(abs))
                    msg.filename = abs.string();
                else
                    msg.filename = raw_file;
            } else {
                msg.filename = raw_file;
            }

            if      (type_str == "error")   msg.type = CompileMessage::CMSG_ERROR;
            else if (type_str == "warning") msg.type = CompileMessage::CMSG_WARNING;
            else if (type_str == "note")    msg.type = CompileMessage::CMSG_NOTE;

            try { msg.line = std::stoi(line_str); } catch (...) {}
            try { msg.col  = std::max(1, std::stoi(col_str)); } catch (...) {}
        }

        messages.push_back(msg);
    }
    return messages;
}

// ---- build-command summaries -----------------------------------------------
// The commands gedi runs are mostly absolute paths. A single-file compile with
// the bundled toolchain runs well past 250 characters, of which the informative
// part - which compiler, which source, which output - is maybe 60. Clipping
// such a line to the width of the Compiler Output pane keeps the uninformative
// head (the toolchain's install directory) and discards exactly the tail the
// user was looking for, so the command gets rewritten instead: every path drops
// to its last component, and if that still doesn't fit, detail switches fold
// into a "(+N more)" counter. What survives even at the narrowest width is
// which tool ran, on what, producing what.

namespace {

// One argument of a command line, with its quoting removed.
struct CmdArg {
    std::string full;                // as written
    std::string brief;               // paths reduced to their last component
    bool        droppable = false;   // a detail switch: shed first when space runs out
};

// Quote-aware split into bare arguments. Quotes only ever protect spaces inside
// a single argument, so they carry nothing once the split has happened.
std::vector<std::string> splitArgs(const std::string& cmd) {
    std::vector<std::string> args;
    std::string cur;
    bool quoted = false, started = false;
    for (char c : cmd) {
        if (c == '"') { quoted = !quoted; started = true; continue; }
        if (!quoted && (c == ' ' || c == '\t')) {
            if (started) { args.push_back(cur); cur.clear(); started = false; }
            continue;
        }
        cur += c;
        started = true;
    }
    if (started) args.push_back(cur);
    return args;
}

// Shell redirections (" 2>&1", "2>NUL") are appended to every command gedi runs
// and say nothing about the build step, so they never reach the summary.
bool isRedirect(const std::string& a) {
    return a.find('>') != std::string::npos || a.find('<') != std::string::npos;
}

// "VAR=value" - an environment override or a make variable, which reads as a
// detail rather than as an operand. A path never qualifies: its separators would
// all sit before the '='.
bool isAssignment(const std::string& a) {
    size_t eq = a.find('=');
    return eq != std::string::npos && eq > 0 &&
           a.find_first_of("/\\") > eq;
}

// Switches that say what is being built, or where; the summary keeps these even
// when it has to shed everything else.
bool isEssentialSwitch(const std::string& a) {
    static const char* const kEssential[] = {
        "-o", "-c", "-C", "-S", "-B", "-f", "-G", "--build", "--target", "--install"
    };
    for (const char* e : kEssential)
        if (a == e) return true;
    return a.rfind("/Fe", 0) == 0;   // cl.exe's output switch
}

// Last component of a path. Anything without a directory part in front of the
// separator is left alone, so cmd.exe's "cd /d" does not lose its "/".
std::string lastComponent(const std::string& s) {
    size_t sep = s.find_last_of("/\\");
    if (sep == std::string::npos || sep == 0 || sep + 1 >= s.size()) return s;
    return s.substr(sep + 1);
}

// Arguments go out unquoted, which would make one holding spaces - a CXXFLAGS=
// assignment, say - read as several. Those get their quotes back.
std::string requote(const std::string& s) {
    if (s.find_first_of(" \t") == std::string::npos) return s;
    return "\"" + s + "\"";
}

// Shorten the path inside one argument. For an operand that is the whole
// argument; for a switch or an assignment only the value half, since the switch
// spelling itself has to stay readable ("-DCMAKE_CXX_COMPILER=clang++.exe",
// "/Fe:hello.exe"). A path attached directly to a short switch ("-I../include")
// is left alone - it is a detail switch, so it gets shed rather than shortened.
std::string shortenArg(const std::string& arg, bool keep_prefix) {
    if (!keep_prefix) return lastComponent(arg);
    size_t eq = arg.find('=');
    if (eq != std::string::npos)
        return arg.substr(0, eq + 1) + lastComponent(arg.substr(eq + 1));
    if (arg[0] == '/') {                            // cl.exe style: /Fe:C:\dir\a.exe
        size_t colon = arg.find(':');
        if (colon != std::string::npos)
            return arg.substr(0, colon + 1) + lastComponent(arg.substr(colon + 1));
    }
    return arg;
}

// The tool's own name, without directory or executable suffix.
std::string toolName(const std::string& arg) {
    std::string name = lastComponent(arg);
    for (const char* ext : {".exe", ".bat", ".cmd", ".com"}) {
        size_t n = std::strlen(ext);
        if (name.size() > n && name.compare(name.size() - n, n, ext) == 0)
            return name.substr(0, name.size() - n);
    }
    return name;
}

std::vector<CmdArg> classifyArgs(const std::vector<std::string>& raw) {
    // cl.exe spells its switches "/W4"; everywhere else a leading '/' is an
    // absolute path, so which one it is depends on the tool being run.
    const bool msvc = !raw.empty() && BuildSystem::isMsvcCompiler(raw[0]);

    std::vector<CmdArg> args;
    for (size_t i = 0; i < raw.size(); ++i) {
        const std::string& a = raw[i];
        if (a.empty() || isRedirect(a)) continue;

        const bool is_switch = a[0] == '-' || (msvc && a[0] == '/');
        const bool assigns   = !is_switch && isAssignment(a);

        CmdArg out;
        out.full      = a;
        out.brief     = args.empty() ? toolName(a) : shortenArg(a, is_switch || assigns);
        out.droppable = !args.empty() && (assigns || (is_switch && !isEssentialSwitch(a)));
        args.push_back(out);
    }
    return args;
}

}  // namespace

// cguess writes a g++ command line whatever compiler gedi is configured with.
// Rebuild it for the configured one, keeping what the guess actually found: the
// sources that belong together and the include paths and macros they need,
// written the way the definition says this compiler takes them.
//   - The guess's own -std= goes: the C++ standard comes from the compile
//     settings, and a second -std later on the line would silently win.
//   - The output lands next to the source, not in whatever directory the
//     editor happened to be started from.
//   - For a compiler that does not build against the host's libraries (cl.exe,
//     Open Watcom, cross compilers), the host's library switches (-l, -L) and
//     its package include directories (/usr/...) mean nothing and are left out.
static std::string retargetCommand(const compilers::Definition& def, const std::string& guess,
                                   const std::string& cxx_in, const std::string& filename)
{
    const std::vector<std::string> args = splitArgs(guess);
    if (args.empty()) return guess;

    const std::string cxx = cxx_in.empty() ? args[0] : cxx_in;
    const bool host = def.host_libraries;

    auto quote = [](const std::string& a) { return "\"" + a + "\""; };
    // Include/define switches stay bare unless they need quoting:
    // getClangArguments picks them out of the command by their prefix.
    auto quoteIfSpaced = [&](const std::string& a) {
        return a.find_first_of(" \t") == std::string::npos ? a : quote(a);
    };

    std::vector<std::string> sources, opts;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-o") { ++i; continue; }                        // replaced below
        if (a.rfind("-std=", 0) == 0) continue;
        if (a.rfind("-I", 0) == 0) {
            if (!host && a.rfind("-I/usr/", 0) == 0) continue;
            opts.push_back(quoteIfSpaced(fill(def.include, "dir", a.substr(2))));
            continue;
        }
        if (a.rfind("-D", 0) == 0) { opts.push_back(quoteIfSpaced(fill(def.define, "def", a.substr(2)))); continue; }
        if (a[0] == '-') { if (host) opts.push_back(a); continue; }
        sources.push_back(a);
    }
    if (sources.empty()) return guess;

    std::error_code ec;
    std::string main = std::filesystem::absolute(filename, ec).lexically_normal().string();
    if (ec) main = filename;
    const std::string stem = main.substr(0, main.find_last_of('.'));

    std::string cmd = quote(cxx) + def.argsString();
    for (const auto& src : sources) cmd += " " + quote(src);
    cmd += " " + outputSwitch(def, stem + def.exeExtension());
    for (const auto& o : opts) cmd += " " + o;
    return cmd;
}

std::string BuildSystem::summarizeCommand(const std::string& command, size_t max_width)
{
    std::vector<CmdArg> args = classifyArgs(splitArgs(command));
    if (args.empty()) return command;

    std::vector<bool> keep(args.size(), true);

    // Dropped arguments collapse into a single counter, placed where the
    // leftmost of them stood.
    auto render = [&args, &keep](bool shorten_paths) {
        const size_t dropped = (size_t)std::count(keep.begin(), keep.end(), false);
        std::string out;
        bool marked = false;
        for (size_t i = 0; i < args.size(); ++i) {
            if (!keep[i] && marked) continue;
            if (!out.empty()) out += ' ';
            if (keep[i]) {
                out += requote(shorten_paths ? args[i].brief : args[i].full);
            } else {
                out += "(+" + std::to_string(dropped) + " more)";
                marked = true;
            }
        }
        return out;
    };

    // Give up as little as possible: the shell noise (quoting, the " 2>&1" every
    // command carries) always goes, but full paths survive while they fit.
    std::string out = render(false);
    if (out.size() <= max_width) return out;

    out = render(true);
    if (out.size() <= max_width) return out;

    // Shed detail switches, rightmost first - the ones nearest the end of the
    // line are the least likely to be what the user is looking for.
    for (size_t i = args.size(); i-- > 0 && out.size() > max_width; ) {
        if (!args[i].droppable) continue;
        keep[i] = false;
        out = render(true);
    }
    if (out.size() <= max_width) return out;

    // Only essentials left and they still don't fit: clip, but say so.
    if (max_width <= 3) return out.substr(0, max_width);
    return out.substr(0, max_width - 3) + "...";
}

std::string BuildSystem::summarizeOutputLine(const std::string& line, size_t max_width)
{
    // The prefixes this class echoes its own commands with. Command lines are
    // always put through the summary, even when they would fit: it also drops
    // the shell plumbing (quoting, the " 2>&1") so that consecutive steps of one
    // build read alike instead of each being formatted by its own length.
    static const char* const kPrefixes[] = { "> ", "Build command: " };
    for (const char* p : kPrefixes) {
        const size_t n = std::strlen(p);
        if (line.compare(0, n, p) != 0) continue;
        if (max_width <= n) break;
        return p + summarizeCommand(line.substr(n), max_width - n);
    }
    return line;   // compiler diagnostics are written to be read; don't touch them
}

// Ask the configured C++ compiler for its builtin/system header search paths and
// return them as "-isystem <dir>" arguments. Without these, libclang can't find
// <stddef.h>/<stdarg.h>/the C++ standard library and bails out with a cascade of
// errors before it ever reaches the user's code. Detected once and cached.
std::vector<std::string> BuildSystem::systemIncludeArgs() {
    {
        std::lock_guard<std::mutex> lk(m_cache_mutex);
        if (m_sys_include_cached) return m_sys_include_args;
    }

    std::vector<std::string> out;
#ifndef _WIN32
    // Only a GCC-style driver answers this question, and only its headers are
    // ones libclang can parse: Open Watcom's are full of __declspec/__watcall
    // that clang rejects. With any other compiler configured, the host's own
    // C++ compiler supplies the headers used for highlighting and completion.
    std::string cxx = m_config.toolchain.cxx;
    if (cxx.empty() || definition().parse_headers != "self") cxx = "c++";
    std::string cmd = cxx + " -xc++ -E -v /dev/null 2>&1";
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char line[4096];
        bool in_list = false;
        while (fgets(line, sizeof(line), p)) {
            std::string s(line);
            if (s.find("#include <...> search starts here:") != std::string::npos) { in_list = true;  continue; }
            if (s.find("End of search list.") != std::string::npos)               { in_list = false; continue; }
            if (!in_list) continue;
            size_t a = s.find_first_not_of(" \t");
            size_t b = s.find_last_not_of(" \t\r\n");
            if (a == std::string::npos || b < a) continue;
            std::string dir = s.substr(a, b - a + 1);
            size_t fw = dir.find(" (framework directory)");   // macOS
            if (fw != std::string::npos) dir = dir.substr(0, fw);
            if (!dir.empty()) { out.push_back("-isystem"); out.push_back(dir); }
        }
        pclose(p);
    }
#endif

    std::lock_guard<std::mutex> lk(m_cache_mutex);
    m_sys_include_args = out;
    m_sys_include_cached = true;
    return out;
}

std::vector<std::string> BuildSystem::getClangArguments(EditorBuffer& buffer) {
    return getClangArguments(buffer.filename, buffer.compiler_settings);
}

// "-Idir" / "-DX" for libclang from an include or define switch in either
// spelling: GCC-style, or cl.exe's "/Idir" "/DX".
static std::string clangIncludeOrDefine(const std::string& a)
{
    if (a.size() > 2 && (a[0] == '-' || a[0] == '/') && (a[1] == 'I' || a[1] == 'D'))
        return std::string("-") + a.substr(1);
    return {};
}

std::string BuildSystem::guessCompileCommand(const std::string& filename) {
    {
        std::lock_guard<std::mutex> lk(m_cache_mutex);
        auto it = m_compile_command_cache.find(filename);
        if (it != m_compile_command_cache.end()) return it->second;
    }

    // Slow path: run cguess outside the lock so other threads aren't blocked.
    std::string cguess_path = "cguess.py";
    if (!std::filesystem::exists(cguess_path) && !m_exe_dir.empty())
        cguess_path = (m_exe_dir / "cguess.py").string();
    if (!std::filesystem::exists(cguess_path) && !m_exe_dir.empty())
        cguess_path = (m_exe_dir.parent_path() / "lib/python3/dist-packages/gedi/cguess.py").string();
    if (!std::filesystem::exists(cguess_path))
        cguess_path = "/usr/local/lib/python3/dist-packages/gedi/cguess.py";

#ifdef _WIN32
    const std::string python = m_config.toolchain.python3.empty() ? "python" : m_config.toolchain.python3;
    std::string cguess_cmd = "\"" + python + "\" \"" + cguess_path + "\" \"" + filename + "\" 2>NUL";
#else
    const std::string python = m_config.toolchain.python3.empty() ? "python3" : m_config.toolchain.python3;
    std::string cguess_cmd = "\"" + python + "\" \"" + cguess_path + "\" \"" + filename + "\" 2>/dev/null";
#endif
    char buffer_arr[512];
    std::string full_cguess_output;

    FILE* cguess_pipe = popen(cguess_cmd.c_str(), "r");
    if (cguess_pipe) {
        while (fgets(buffer_arr, sizeof(buffer_arr), cguess_pipe) != NULL)
            full_cguess_output += buffer_arr;
        pclose(cguess_pipe);
    }

    std::string result;
    std::size_t found = full_cguess_output.find("GUESS: ");
    if (found != std::string::npos) {
        result = full_cguess_output.substr(found + 7);
        if (!result.empty() && result.back() == '\n') result.pop_back();
    }

    // Fallback: use the toolchain-discovered C++ compiler, or g++ if unknown.
    if (result.empty())
        result = defaultCompileCommand(definition(), m_config.toolchain.cxx, filename);
    else
        result = retargetCommand(definition(), result, m_config.toolchain.cxx, filename);

    {
        std::lock_guard<std::mutex> lk(m_cache_mutex);
        m_compile_command_cache.emplace(filename, result);
    }
    return result;
}

std::vector<std::string> BuildSystem::getClangArguments(const std::string& filename,
                                                        const CompilerSettings& settings) {
    std::vector<std::string> args;
    args.push_back("-xc++");
    args.push_back("-std=" + (settings.cpp_standard.empty() ? "c++20" : settings.cpp_standard));
    args.push_back("-I.");
    for (auto& a : systemIncludeArgs()) args.push_back(a);

    // Include directories and macros from the configured compiler's optional
    // flags, and from the command the guess produced.
    auto pick = [&](const std::string& text) {
        std::stringstream ss(text);
        std::string part;
        while (ss >> part) {
            if (part.size() > 1 && part.front() == '"' && part.back() == '"')
                part = part.substr(1, part.size() - 2);
            const std::string a = clangIncludeOrDefine(part);
            if (!a.empty()) args.push_back(a);
        }
    };
    pick(compilers::optionalFlags(settings, definition()));
    pick(guessCompileCommand(filename));
    return args;
}

std::string BuildSystem::get_full_compile_command(const std::string& base_command, const CompilerSettings& settings) {
    if (base_command.empty()) return "";

    const compilers::Definition& def = definition();
    std::string flags = def.standardFlag(settings.cpp_standard);
    const std::string rest = compilers::flagsFor(settings, def, false);
    if (!rest.empty()) flags += (flags.empty() ? "" : " ") + rest;
    if (flags.empty()) return base_command;

    // Split right after the compiler token, not on the first bare space - a
    // Windows compiler path like "C:\Program Files\...\cl.exe" is itself
    // quoted and contains spaces, so naively splitting on the first space
    // would cut the command in half mid-path.
    size_t token_end;
    if (base_command.front() == '"') {
        size_t close = base_command.find('"', 1);
        token_end = (close == std::string::npos) ? base_command.size() : close + 1;
    } else {
        size_t sp = base_command.find(' ');
        token_end = (sp == std::string::npos) ? base_command.size() : sp;
    }

    // ...and after the definition's own arguments, which belong to the
    // executable ("zig c++ <flags>", not "zig <flags> c++").
    const std::string args = def.argsString();
    if (!args.empty() && base_command.compare(token_end, args.size(), args) == 0)
        token_end += args.size();

    return base_command.substr(0, token_end) + " " + flags + base_command.substr(token_end);
}
