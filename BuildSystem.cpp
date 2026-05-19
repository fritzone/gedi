#include "BuildSystem.h"
#include <cstdio>
#include <sstream>
#include <regex>
#include <filesystem>
#include <sys/wait.h>

BuildSystem::BuildSystem(const Config& config) : m_config(config) {}

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
    if (o_pos != std::string::npos) {
        std::string temp = result.full_command.substr(o_pos + 3);
        result.executable_name = temp.substr(0, temp.find(' '));
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

    if (project.build_system == "cmake") {
        // Locate existing configured build directory
        for (const char* candidate : {"build", "cmake-build-debug", "cmake-build-release"}) {
            std::string d = root + "/" + candidate;
            if (std::filesystem::exists(d + "/CMakeCache.txt")) { build_dir = d; break; }
        }
        if (build_dir.empty()) {
            build_dir = root + "/build";
            result.output_lines.push_back("No CMake build directory found — configuring...");
            if (!run_cmd("cmake -S \"" + root + "\" -B \"" + build_dir + "\" 2>&1")) {
                result.output_lines.push_back("=== CMake configure failed ===");
                return result;
            }
            result.output_lines.push_back("");
        }
        build_cmd = "cmake --build \"" + build_dir + "\" 2>&1";
        result.executable_name = build_dir + "/" + project.name;

    } else if (project.build_system == "make") {
        build_dir  = root;
        build_cmd  = "make -C \"" + root + "\" 2>&1";
        result.executable_name = root + "/" + project.name;

    } else if (project.build_system == "meson") {
        build_dir = root + "/builddir";
        if (!std::filesystem::exists(build_dir + "/build.ninja")) {
            result.output_lines.push_back("No Meson build directory found — setting up...");
            if (!run_cmd("meson setup \"" + build_dir + "\" \"" + root + "\" 2>&1")) {
                result.output_lines.push_back("=== Meson setup failed ===");
                return result;
            }
            result.output_lines.push_back("");
        }
        build_cmd = "ninja -C \"" + build_dir + "\" 2>&1";
        result.executable_name = build_dir + "/" + project.name;

    } else {
        result.output_lines.push_back("Unknown build system: " + project.build_system);
        return result;
    }

    result.success = run_cmd(build_cmd);
    result.output_lines.push_back("");
    result.output_lines.push_back(result.success ? "=== Build successful ===" : "=== Build failed ===");
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
    if (m_compile_command_cache.count(filename))
        return m_compile_command_cache[filename];

    std::string cguess_cmd = "python3 /usr/local/lib/python3/dist-packages/gedi/cguess.py \"" + filename + "\" 2>/dev/null";
    char buffer_arr[512];
    std::string full_cguess_output;

    FILE* cguess_pipe = popen(cguess_cmd.c_str(), "r");
    if (cguess_pipe) {
        while (fgets(buffer_arr, sizeof(buffer_arr), cguess_pipe) != NULL)
            full_cguess_output += buffer_arr;
        pclose(cguess_pipe);
    }

    std::size_t found = full_cguess_output.find("GUESS: ");
    if (found != std::string::npos) {
        std::string base_compile_cmd = full_cguess_output.substr(found + 7);
        if (!base_compile_cmd.empty() && base_compile_cmd.back() == '\n')
            base_compile_cmd.pop_back();
        m_compile_command_cache[filename] = base_compile_cmd;
        return base_compile_cmd;
    }
    return "";
}

std::string BuildSystem::get_full_compile_command(const std::string& base_command, const CompilerSettings& settings) {
    if (base_command.empty()) return "";

    std::string flags;
    flags += "-std=" + settings.cpp_standard + " ";

    if (settings.debug_symbols) flags += "-g ";
    if (settings.optimization_level == 0) flags += "-O0 ";
    else if (settings.optimization_level == 1) flags += "-O2 ";
    else if (settings.optimization_level == 2) flags += "-O3 ";

    if (settings.wall)     flags += "-Wall ";
    if (settings.wextra)   flags += "-Wextra ";
    if (settings.wpedantic) flags += "-Wpedantic ";
    if (settings.werror)   flags += "-Werror ";

    if (settings.wconversion)        flags += "-Wconversion ";
    if (settings.wsign_conversion)   flags += "-Wsign-conversion ";
    if (settings.wshadow)            flags += "-Wshadow ";
    if (settings.wnon_virtual_dtor)  flags += "-Wnon-virtual-dtor ";
    if (settings.wold_style_cast)    flags += "-Wold-style-cast ";
    if (settings.woverloaded_virtual) flags += "-Woverloaded-virtual ";
    if (settings.wnull_dereference)  flags += "-Wnull-dereference ";
    if (settings.wdouble_promotion)  flags += "-Wdouble-promotion ";
    if (settings.wformat_2)          flags += "-Wformat=2 ";

    if (settings.fno_omit_frame_pointer)  flags += "-fno-omit-frame-pointer ";
    if (settings.fsanitize_address_ub)    flags += "-fsanitize=address,undefined ";
    if (settings.fsanitize_leak)          flags += "-fsanitize=leak ";
    if (settings.flto)                    flags += "-flto ";
    if (settings.march_native)            flags += "-march=native ";
    if (settings.mtune_native)            flags += "-mtune=native ";

    if (settings.wcast_align)         flags += "-Wcast-align ";
    if (settings.wcast_qual)          flags += "-Wcast-qual ";
    if (settings.wswitch_enum)        flags += "-Wswitch-enum ";
    if (settings.wundef)              flags += "-Wundef ";
    if (settings.wredundant_decls)    flags += "-Wredundant-decls ";
    if (settings.wlogical_op)         flags += "-Wlogical-op ";
    if (settings.wuseless_cast)       flags += "-Wuseless-cast ";
    if (settings.weffcxx)             flags += "-Weffc++ ";

    if (settings.fno_exceptions)          flags += "-fno-exceptions ";
    if (settings.fno_rtti)                flags += "-fno-rtti ";
    if (settings.fvisibility_hidden)      flags += "-fvisibility=hidden ";
    if (settings.fstrict_aliasing)        flags += "-fstrict-aliasing ";
    if (settings.fsanitize_pointer_compare)  flags += "-fsanitize=pointer-compare ";
    if (settings.fsanitize_pointer_subtract) flags += "-fsanitize=pointer-subtract ";
    if (settings.wl_as_needed)            flags += "-Wl,--as-needed ";
    if (settings.wl_o1)                   flags += "-Wl,-O1 ";

    if (!settings.optional_flags.empty())
        flags += settings.optional_flags + " ";

    size_t compiler_pos = base_command.find(' ');
    if (compiler_pos == std::string::npos) return base_command + " " + flags;
    return base_command.substr(0, compiler_pos) + " " + flags + " " + base_command.substr(compiler_pos + 1);
}
