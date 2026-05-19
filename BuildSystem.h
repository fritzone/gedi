#ifndef BUILDSYSTEM_H
#define BUILDSYSTEM_H

#include <string>
#include <vector>
#include <map>
#include "EditorBuffer.h"
#include "ConfigManager.h"
#include "CompilerSettings.h"
#include "GediProject.h"

struct CompilationResult {
    std::vector<std::string> output_lines;
    std::string executable_name;
    bool success;
    std::string full_command;
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
    BuildSystem(const Config& config);

    CompilationResult runCompilationProcess(EditorBuffer& buffer);
    CompilationResult runProjectBuild(const GediProject& project);
    std::vector<CompileMessage> parseCompilerOutput(const std::string& full_output_str,
                                                    std::vector<std::string>& output_lines_out,
                                                    const std::string& base_dir = "");

    void setConfig(const Config& config) { m_config = config; }
    void invalidateCache(const std::string& filename) { m_compile_command_cache.erase(filename); }

    std::vector<std::string> getClangArguments(EditorBuffer& buffer);
    std::string guessCompileCommand(const std::string& filename);
    std::string get_full_compile_command(const std::string& base_command, const CompilerSettings& settings);

private:
    Config m_config;
    std::map<std::string, std::string> m_compile_command_cache;
};

#endif // BUILDSYSTEM_H
