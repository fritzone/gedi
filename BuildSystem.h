#ifndef BUILDSYSTEM_H
#define BUILDSYSTEM_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "EditorBuffer.h"
#include "ConfigManager.h"
#include "CompilerSettings.h"
#include "GediProject.h"

struct CompilationResult {
    std::vector<std::string> output_lines;
    std::string executable_name;
    bool success;
    std::string full_command;
    std::string temp_exe;  // if set, delete this executable after running
};

struct CompileMessage {
    enum CompileMessageType { CMSG_NONE, CMSG_ERROR, CMSG_WARNING, CMSG_NOTE };
    std::string full_text;
    std::string filename;   // absolute path to source file, empty if unknown
    CompileMessageType type = CMSG_NONE;
    int line = -1;
    int col  = -1;
};

class BuildSystem {
public:
    BuildSystem(const Config& config, std::filesystem::path exe_dir = "");

    CompilationResult runCompilationProcess(EditorBuffer& buffer);
    CompilationResult runProjectBuild(const GediProject& project);
    std::vector<CompileMessage> parseCompilerOutput(const std::string& full_output_str,
                                                    std::vector<std::string>& output_lines_out,
                                                    const std::string& base_dir = "");

    void setConfig(const Config& config) { m_config = config; }
    void invalidateCache(const std::string& filename) { m_compile_command_cache.erase(filename); }

    std::vector<std::string> getClangArguments(EditorBuffer& buffer);
    // Thread-safe overload: call this from background threads.
    std::vector<std::string> getClangArguments(const std::string& filename,
                                               const CompilerSettings& settings);
    std::string guessCompileCommand(const std::string& filename);
    std::string get_full_compile_command(const std::string& base_command, const CompilerSettings& settings);

    // Convert CompilerSettings to a flat flags string (e.g. "-g -O0 -Wall -Wextra")
    static std::string settingsToFlags(const CompilerSettings& s);

    // Human-readable build command preview used by the Build Options dialog
    static std::string buildProjectPreview(const GediProject& project, const CompilerSettings& settings);

private:
    Config m_config;
    std::filesystem::path m_exe_dir;
    std::mutex m_cache_mutex;
    std::map<std::string, std::string> m_compile_command_cache;
};

#endif // BUILDSYSTEM_H
