#include "BuildSystem.h"
#include "platform_compat.h"
#include <cstdio>
#include <cctype>
#include <sstream>
#include <regex>
#include <filesystem>

BuildSystem::BuildSystem(const Config& config, std::filesystem::path exe_dir)
    : m_config(config), m_exe_dir(exe_dir) {}

bool BuildSystem::isMsvcCompiler(const std::string& compilerPath) {
    std::string p = compilerPath;
    if (p.size() >= 2 && p.front() == '"' && p.back() == '"')
        p = p.substr(1, p.size() - 2);
    std::string stem = std::filesystem::path(p).stem().string();
    for (auto& c : stem) c = (char)std::tolower((unsigned char)c);
    return stem == "cl";
}

// First whitespace-delimited (quote-aware) token of a shell command line —
// the compiler executable.
static std::string firstCommandToken(const std::string& cmd) {
    if (cmd.empty()) return "";
    if (cmd.front() == '"') {
        size_t end = cmd.find('"', 1);
        if (end != std::string::npos) return cmd.substr(1, end - 1);
    }
    size_t sp = cmd.find(' ');
    return (sp == std::string::npos) ? cmd : cmd.substr(0, sp);
}

// "c++17" -> "/std:c++17" for cl.exe, "-std=c++17" for GCC/Clang. cl.exe has
// no dialect switch below C++14, so older standards just take the compiler's
// default rather than passing something it will reject.
static std::string stdFlag(const std::string& cpp_standard, bool msvc) {
    if (!msvc) return "-std=" + cpp_standard;
    if (cpp_standard == "c++98" || cpp_standard == "c++03" || cpp_standard == "c++11")
        return "";
    return "/std:" + cpp_standard;
}

CompilationResult BuildSystem::runCompilationProcess(EditorBuffer& buffer) {
    CompilationResult result;
    result.success = false;

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

    size_t o_pos = result.full_command.find("-o ");
    size_t fe_pos = result.full_command.find("/Fe:");
    if (o_pos != std::string::npos) {
        std::string temp = result.full_command.substr(o_pos + 3);
        result.executable_name = temp.substr(0, temp.find(' '));
    } else if (fe_pos != std::string::npos) {
        std::string temp = result.full_command.substr(fe_pos + 4);
        if (!temp.empty() && temp.front() == '"') {
            size_t end = temp.find('"', 1);
            result.executable_name = temp.substr(1, end == std::string::npos ? std::string::npos : end - 1);
        } else {
            result.executable_name = temp.substr(0, temp.find(' '));
        }
    } else {
        result.executable_name = "a.out";
    }

    std::string full_compiler_output_str;
    char buffer_arr[512];
    FILE* compile_pipe = popen((result.full_command + " 2>&1").c_str(), "r");
    if (compile_pipe) {
        while (fgets(buffer_arr, sizeof(buffer_arr), compile_pipe) != NULL)
            full_compiler_output_str += buffer_arr;
    }
    int compile_status = compile_pipe ? pclose(compile_pipe) : -1;
    result.success = (compile_status == 0);

    // Add compiler output to output_lines for display
    std::istringstream ss(full_compiler_output_str);
    std::string line;
    while (std::getline(ss, line))
        result.output_lines.push_back(line);

    return result;
}

std::string BuildSystem::settingsToFlags(const CompilerSettings& s, bool msvc)
{
    std::string f;

    if (msvc) {
        // cl.exe has no per-warning switches matching GCC's -W* catalogue; a
        // request for any of them just bumps the overall warning level.
        bool any_extra_warn = s.wextra || s.wconversion || s.wsign_conversion || s.wshadow ||
            s.wnon_virtual_dtor || s.wold_style_cast || s.woverloaded_virtual ||
            s.wnull_dereference || s.wdouble_promotion || s.wformat_2 ||
            s.wcast_align || s.wcast_qual || s.wswitch_enum || s.wundef ||
            s.wredundant_decls || s.wlogical_op || s.wuseless_cast || s.weffcxx;
        f += (s.wall || any_extra_warn) ? "/W4 " : "/W3 ";
        if (s.wpedantic) f += "/permissive- ";
        if (s.werror)    f += "/WX ";

        if (s.debug_symbols) f += "/Zi ";
        if (s.optimization_level == 0)      f += "/Od ";
        else if (s.optimization_level == 1) f += "/O2 ";
        else if (s.optimization_level == 2) f += "/Ox ";

        f += s.fno_exceptions ? "" : "/EHsc ";
        if (s.fno_rtti)             f += "/GR- ";
        if (s.fsanitize_address_ub) f += "/fsanitize=address ";

        // No cl.exe equivalent for these GCC/Clang-specific options, so they're
        // intentionally dropped rather than passed through broken:
        // -flto, -march/-mtune=native, -fvisibility=hidden, -fstrict-aliasing,
        // -fsanitize=leak/pointer-compare/pointer-subtract, -Wl,...

        if (!s.optional_flags.empty()) f += s.optional_flags + " ";
        if (!f.empty() && f.back() == ' ') f.pop_back();
        return f;
    }

    if (s.debug_symbols)         f += "-g ";
    if (s.optimization_level==0) f += "-O0 ";
    else if (s.optimization_level==1) f += "-O2 ";
    else if (s.optimization_level==2) f += "-O3 ";
    if (s.wall)            f += "-Wall ";
    if (s.wextra)          f += "-Wextra ";
    if (s.wpedantic)       f += "-Wpedantic ";
    if (s.werror)          f += "-Werror ";
    if (s.wconversion)     f += "-Wconversion ";
    if (s.wsign_conversion)f += "-Wsign-conversion ";
    if (s.wshadow)         f += "-Wshadow ";
    if (s.wnon_virtual_dtor)  f += "-Wnon-virtual-dtor ";
    if (s.wold_style_cast) f += "-Wold-style-cast ";
    if (s.woverloaded_virtual) f += "-Woverloaded-virtual ";
    if (s.wnull_dereference)   f += "-Wnull-dereference ";
    if (s.wdouble_promotion)   f += "-Wdouble-promotion ";
    if (s.wformat_2)       f += "-Wformat=2 ";
    if (s.fno_omit_frame_pointer) f += "-fno-omit-frame-pointer ";
    if (s.fsanitize_address_ub)   f += "-fsanitize=address,undefined ";
    if (s.fsanitize_leak)  f += "-fsanitize=leak ";
    if (s.flto)            f += "-flto ";
    if (s.march_native)    f += "-march=native ";
    if (s.mtune_native)    f += "-mtune=native ";
    if (s.wcast_align)     f += "-Wcast-align ";
    if (s.wcast_qual)      f += "-Wcast-qual ";
    if (s.wswitch_enum)    f += "-Wswitch-enum ";
    if (s.wundef)          f += "-Wundef ";
    if (s.wredundant_decls)f += "-Wredundant-decls ";
    if (s.wlogical_op)     f += "-Wlogical-op ";
    if (s.wuseless_cast)   f += "-Wuseless-cast ";
    if (s.weffcxx)         f += "-Weffc++ ";
    if (s.fno_exceptions)  f += "-fno-exceptions ";
    if (s.fno_rtti)        f += "-fno-rtti ";
    if (s.fvisibility_hidden) f += "-fvisibility=hidden ";
    if (s.fstrict_aliasing)   f += "-fstrict-aliasing ";
    if (s.fsanitize_pointer_compare)  f += "-fsanitize=pointer-compare ";
    if (s.fsanitize_pointer_subtract) f += "-fsanitize=pointer-subtract ";
    if (s.wl_as_needed)    f += "-Wl,--as-needed ";
    if (s.wl_o1)           f += "-Wl,-O1 ";
    if (!s.optional_flags.empty()) f += s.optional_flags + " ";
    if (!f.empty() && f.back() == ' ') f.pop_back();
    return f;
}

// ── buildProjectPreview ───────────────────────────────────────────────────────

std::string BuildSystem::buildProjectPreview(const GediProject& project, const CompilerSettings& s) const
{
    const std::string& root = project.root;
    std::string std_num = s.cpp_standard;
    if (std_num.rfind("c++", 0) == 0) std_num = std_num.substr(3);

    // cmake build type
    std::string bt;
    if (s.debug_symbols && s.optimization_level == 0)  bt = "Debug";
    else if (s.debug_symbols && s.optimization_level > 0) bt = "RelWithDebInfo";
    else if (s.optimization_level == 2) bt = "Release";
    else bt = "Debug";

    bool msvc = isMsvcCompiler(m_config.toolchain.cxx);
    std::string flags = settingsToFlags(s, msvc);

    if (project.build_system == "cmake") {
        std::string build_dir = root + "/build";
        std::string args;
        args += " -DCMAKE_BUILD_TYPE=" + bt;
        args += " -DCMAKE_CXX_STANDARD=" + std_num;
        if (!flags.empty()) args += " \"-DCMAKE_CXX_FLAGS=" + flags + "\"";
        return "cmake -S \"" + root + "\" -B \"" + build_dir + "\"" + args + "\n"
             + "cmake --build \"" + build_dir + "\"";
    }
    if (project.build_system == "make") {
        std::string cxxflags = "-std=" + s.cpp_standard;
        if (!flags.empty()) cxxflags += " " + flags;
        return "make -C \"" + root + "\" CXXFLAGS=\"" + cxxflags + "\"";
    }
    if (project.build_system == "meson") {
        std::string build_dir = root + "/builddir";
        std::string buildtype = (bt == "Release") ? "release" : "debug";
        std::string cxxflags = "-std=" + s.cpp_standard;
        if (!flags.empty()) cxxflags += " " + flags;
        return "meson setup \"" + build_dir + "\" \"" + root + "\" --buildtype=" + buildtype + "\n"
             + "CXXFLAGS=\"" + cxxflags + "\" ninja -C \"" + build_dir + "\"";
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
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) {
            result.output_lines.push_back("  [failed to start process]");
            return false;
        }
        while (fgets(buf, sizeof(buf), p))
            out += buf;
        int status = pclose(p);
        std::istringstream ss(out);
        std::string ln;
        while (std::getline(ss, ln))
            result.output_lines.push_back(ln);
        return (status == 0);
    };

    result.output_lines.push_back("=== Building project: " + project.name + " ===");

    const CompilerSettings& cs = project.compiler_settings;

    // cmake build type string
    std::string bt;
    if (cs.debug_symbols && cs.optimization_level == 0)  bt = "Debug";
    else if (cs.debug_symbols && cs.optimization_level > 0) bt = "RelWithDebInfo";
    else if (cs.optimization_level == 2) bt = "Release";
    else bt = "Debug";

    std::string std_num = cs.cpp_standard;
    if (std_num.rfind("c++", 0) == 0) std_num = std_num.substr(3);

    bool msvc = isMsvcCompiler(m_config.toolchain.cxx);
    std::string extra_flags = settingsToFlags(cs, msvc);

    if (project.build_system == "cmake") {
        // Locate or create build directory
        for (const char* candidate : {"build", "cmake-build-debug", "cmake-build-release"}) {
            std::string d = root + "/" + candidate;
            if (std::filesystem::exists(d + "/CMakeCache.txt")) { build_dir = d; break; }
        }
        if (build_dir.empty()) {
            build_dir = root + "/build";
            result.output_lines.push_back("No CMake build directory found — configuring...");
        }
        // Always (re)configure so setting changes are picked up
        std::string cmake_args;
        cmake_args += " -DCMAKE_BUILD_TYPE=" + bt;
        cmake_args += " -DCMAKE_CXX_STANDARD=" + std_num;
        if (!extra_flags.empty())
            cmake_args += " \"-DCMAKE_CXX_FLAGS=" + extra_flags + "\"";
        if (!run_cmd("cmake -S \"" + root + "\" -B \"" + build_dir + "\"" + cmake_args + " 2>&1")) {
            result.output_lines.push_back("=== CMake configure failed ===");
            return result;
        }
        result.output_lines.push_back("");
        build_cmd = "cmake --build \"" + build_dir + "\" 2>&1";
        result.executable_name = build_dir + "/" + project.name;

    } else if (project.build_system == "make") {
        build_dir = root;
        std::string cxxflags = "-std=" + cs.cpp_standard;
        if (!extra_flags.empty()) cxxflags += " " + extra_flags;
        build_cmd = "make -C \"" + root + "\" CXXFLAGS=\"" + cxxflags + "\" 2>&1";
        result.executable_name = root + "/" + project.name;

    } else if (project.build_system == "meson") {
        build_dir = root + "/builddir";
        std::string buildtype = (bt == "Release") ? "release" : "debug";
        if (!std::filesystem::exists(build_dir + "/build.ninja")) {
            result.output_lines.push_back("No Meson build directory found — setting up...");
            if (!run_cmd("meson setup \"" + build_dir + "\" \"" + root + "\" --buildtype=" + buildtype + " 2>&1")) {
                result.output_lines.push_back("=== Meson setup failed ===");
                return result;
            }
            result.output_lines.push_back("");
        } else {
            // Update build type if project already configured
            run_cmd("meson configure \"" + build_dir + "\" --buildtype=" + buildtype + " 2>&1");
        }
        std::string cxxflags = "-std=" + cs.cpp_standard;
        if (!extra_flags.empty()) cxxflags += " " + extra_flags;
        build_cmd = "CXXFLAGS=\"" + cxxflags + "\" ninja -C \"" + build_dir + "\" 2>&1";
        result.executable_name = build_dir + "/" + project.name;

    } else {
        result.output_lines.push_back("Unknown build system: " + project.build_system);
        return result;
    }

    result.success = run_cmd(build_cmd);
    result.output_lines.push_back("");
    result.output_lines.push_back(result.success ? "=== Build successful ===" : "=== Build failed ===");

    if (result.success) {
        // Replace the pre-build guess with the actual binary location — the
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

    while (std::getline(stream, line)) {
        output_lines_out.push_back(line);
        CompileMessage msg;
        msg.full_text = line;

        std::smatch match;
        if (std::regex_search(line, match, re_diag)) {
            std::string raw_file = match[1].str();

            // Resolve filename to absolute path
            if (!raw_file.empty() && raw_file[0] == '/') {
                msg.filename = raw_file;
            } else if (!base_dir.empty()) {
                std::string candidate = base_dir + "/" + raw_file;
                std::error_code ec;
                auto abs = std::filesystem::weakly_canonical(candidate, ec);
                if (!ec && std::filesystem::exists(abs))
                    msg.filename = abs.string();
                else
                    msg.filename = raw_file;
            } else {
                msg.filename = raw_file;
            }

            const std::string& type_str = match[4].str();
            if      (type_str == "error")   msg.type = CompileMessage::CMSG_ERROR;
            else if (type_str == "warning") msg.type = CompileMessage::CMSG_WARNING;
            else if (type_str == "note")    msg.type = CompileMessage::CMSG_NOTE;

            try { msg.line = std::stoi(match[2].str()); } catch (...) {}
            try { msg.col  = std::stoi(match[3].str()); } catch (...) {}
        }

        messages.push_back(msg);
    }
    return messages;
}

std::vector<std::string> BuildSystem::getClangArguments(EditorBuffer& buffer) {
    std::vector<std::string> args;
    args.push_back("-xc++");
    args.push_back("-std=" + (buffer.compiler_settings.cpp_standard.empty() ? "c++20" : buffer.compiler_settings.cpp_standard));

    args.push_back("-I.");
    args.push_back("-I/usr/include");
    args.push_back("-I/usr/local/include");

    if (!buffer.compiler_settings.optional_flags.empty()) {
        std::stringstream ss(buffer.compiler_settings.optional_flags);
        std::string flag;
        while (ss >> flag) {
            if (flag.rfind("-I", 0) == 0 || flag.rfind("-D", 0) == 0)
                args.push_back(flag);
        }
    }

    std::string base_cmd = guessCompileCommand(buffer.filename);
    if (!base_cmd.empty()) {
        std::stringstream ss(base_cmd);
        std::string part;
        while (ss >> part) {
            if (part.rfind("-I", 0) == 0 || part.rfind("-D", 0) == 0)
                args.push_back(part);
        }
    }

    return args;
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
    if (result.empty()) {
        const std::string cxx = m_config.toolchain.cxx.empty() ? "g++" : m_config.toolchain.cxx;
        std::string stem = filename.substr(0, filename.find_last_of('.'));
        if (isMsvcCompiler(cxx))
            result = "\"" + cxx + "\" \"" + filename + "\" /Fe:\"" + stem + ".exe\"";
        else
            result = "\"" + cxx + "\" \"" + filename + "\" -o \"" + stem + "\"";
    }

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
    args.push_back("-I/usr/include");
    args.push_back("-I/usr/local/include");

    if (!settings.optional_flags.empty()) {
        std::stringstream ss(settings.optional_flags);
        std::string flag;
        while (ss >> flag) {
            if (flag.rfind("-I", 0) == 0 || flag.rfind("-D", 0) == 0)
                args.push_back(flag);
        }
    }

    std::string base_cmd = guessCompileCommand(filename);
    if (!base_cmd.empty()) {
        std::stringstream ss(base_cmd);
        std::string part;
        while (ss >> part) {
            if (part.rfind("-I", 0) == 0 || part.rfind("-D", 0) == 0)
                args.push_back(part);
        }
    }
    return args;
}

std::string BuildSystem::get_full_compile_command(const std::string& base_command, const CompilerSettings& settings) {
    if (base_command.empty()) return "";

    bool msvc = isMsvcCompiler(firstCommandToken(base_command));

    std::string flags = stdFlag(settings.cpp_standard, msvc);
    std::string rest  = settingsToFlags(settings, msvc);
    if (!rest.empty()) flags += (flags.empty() ? "" : " ") + rest;
    if (flags.empty()) return base_command;

    // Split right after the compiler token, not on the first bare space — a
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

    return base_command.substr(0, token_end) + " " + flags + base_command.substr(token_end);
}
