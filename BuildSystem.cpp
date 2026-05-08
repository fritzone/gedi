#include "BuildSystem.h"
#include <cstdio>
#include <sstream>
#include <regex>
#include <iostream>

BuildSystem::BuildSystem(const Config& config) : m_config(config) {}

CompilationResult BuildSystem::runCompilationProcess(EditorBuffer& buffer) {
    CompilationResult result;
    result.success = false;

    std::string base_compile_cmd;

    // --- Step 1: Check cache or run cguess.py ---
    if (m_compile_command_cache.count(buffer.filename)) {
        base_compile_cmd = m_compile_command_cache[buffer.filename];
        result.output_lines.push_back("Using cached build command...");
    } else {
        result.output_lines.push_back("Running cguess.py to find build command...");
        std::string cguess_cmd = "python3 /usr/local/lib/python3/dist-packages/gedi/cguess.py \"" + buffer.filename + "\" 2>&1";
        char buffer_arr[512];

        FILE* cguess_pipe = popen(cguess_cmd.c_str(), "r");
        std::string full_cguess_output;
        if (cguess_pipe) {
            while (fgets(buffer_arr, sizeof(buffer_arr), cguess_pipe) != NULL) {
                full_cguess_output += buffer_arr;
            }
            pclose(cguess_pipe);
        }

        std::size_t found = full_cguess_output.find("GUESS: ");
        if (found != std::string::npos) {
            base_compile_cmd = full_cguess_output.substr(found + 7);
            if (!base_compile_cmd.empty() && base_compile_cmd.back() == '\n') base_compile_cmd.pop_back();
            m_compile_command_cache[buffer.filename] = base_compile_cmd;
            result.output_lines.push_back("Found: " + base_compile_cmd);
        } else {
            result.output_lines.push_back("cguess.py failed or returned no command.");
            result.output_lines.push_back("Full cguess output: " + full_cguess_output);
            return result;
        }
    }

    // Step 2: Run Compiler
    result.full_command = get_full_compile_command(base_compile_cmd, m_config.compile_mode, m_config.optimization_level, m_config.security_flags, m_config.extra_compile_flags);
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
        while (fgets(buffer_arr, sizeof(buffer_arr), compile_pipe) != NULL) {
            full_compiler_output_str += buffer_arr;
        }
    }
    int compile_status = compile_pipe ? pclose(compile_pipe) : -1;
    result.success = (compile_status == 0);

    return result;
}

std::vector<CompileMessage> BuildSystem::parseCompilerOutput(const std::string& full_output_str, std::vector<std::string>& output_lines_out) {
    std::vector<CompileMessage> messages;
    std::istringstream stream(full_output_str);
    std::string line;

    while(std::getline(stream, line)){
        output_lines_out.push_back(line);
        std::smatch match;
        std::regex re_error_loc(R"(([^:]+):(\d+):(\d+):\s+(.+))");
        if (std::regex_search(line, match, re_error_loc)) {
            CompileMessage msg; msg.full_text = line;
            if (line.find("error:") != std::string::npos) msg.type = CompileMessage::CMSG_ERROR;
            else if (line.find("warning:") != std::string::npos) msg.type = CompileMessage::CMSG_WARNING;
            try { msg.line = std::stoi(match[2]); msg.col = std::stoi(match[3]); } catch(...) {}
            messages.push_back(msg);
        } else {
            messages.push_back({ line, CompileMessage::CMSG_NONE });
        }
    }
    return messages;
}

std::string BuildSystem::get_full_compile_command(const std::string& base_command, int mode, int opt_level, const std::vector<bool>& security_flags, const std::string& extra_flags) {
    if (base_command.empty()) return "";

    std::string flags;
    if (mode == 0) { // Debug
        flags += "-g ";
    } else if (mode == 1) { // Release
        flags += "-DNDEBUG ";
    }

    switch (opt_level) {
    case 0: flags += "-O0 "; break;
    case 1: flags += "-O1 "; break;
    case 2: flags += "-O2 "; break;
    case 3: flags += "-O3 "; break;
    case 4: flags += "-Os "; break;
    case -1: // No optimization
    default:
        break;
    }

    const std::vector<std::string> sec_flag_strings = {
        "-fstack-protector-strong", // Stack Protector
        "-fPIE -pie",               // PIE
        "-D_FORTIFY_SOURCE=2",      // Fortify Source
        "-fstack-clash-protection", // Stack Clash
        "-Wl,-z,relro,-z,now"       // RELRO
    };

    for(size_t i = 0; i < security_flags.size() && i < sec_flag_strings.size(); ++i) {
        if (security_flags[i]) {
            flags += sec_flag_strings[i] + " ";
        }
    }

    flags += extra_flags;

    size_t compiler_pos = base_command.find(' ');
    if (compiler_pos == std::string::npos) return base_command + " " + flags;

    return base_command.substr(0, compiler_pos) + " " + flags + " " + base_command.substr(compiler_pos + 1);
}
