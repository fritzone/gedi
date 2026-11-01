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

    // The configured compiler has changed. Everything derived from it is now
    // stale: the per-file compile commands name the old compiler, and the
    // -isystem list was asked of it. Both are rebuilt on next use.
    void toolchainChanged();

    std::vector<std::string> getClangArguments(EditorBuffer& buffer);
    // Thread-safe overload: call this from background threads.
    std::vector<std::string> getClangArguments(const std::string& filename,
                                               const CompilerSettings& settings);
    std::string guessCompileCommand(const std::string& filename);
    std::string get_full_compile_command(const std::string& base_command, const CompilerSettings& settings);

    // The full compile command for `filename` as it would be with `cxx` as the
    // compiler instead of the configured one. The Build Options dialog previews
    // a compiler the user has picked but not yet accepted; re-deriving the
    // command properly would mean re-running the (slow, cached) command guess on
    // every repaint. An empty `cxx` means "the configured one".
    std::string previewWithCompiler(const std::string& filename,
                                    const CompilerSettings& settings,
                                    const std::string& cxx);

    // Convert CompilerSettings to a flat flags string. Produces GCC/Clang-style
    // flags (e.g. "-g -O0 -Wall -Wextra") or, when msvc is true, cl.exe-style
    // flags (e.g. "/Zi /Od /W4") - the two option languages are not
    // interchangeable, and handing GCC flags to cl.exe fails hard (e.g. it
    // parses "-Wextra" as "/W" with a non-numeric argument).
    static std::string settingsToFlags(const CompilerSettings& s, bool msvc);

    // True if the given compiler path/name refers to MSVC's cl.exe.
    static bool isMsvcCompiler(const std::string& compilerPath);

    // A build command condensed to at most max_width columns: paths shrink to
    // their last component and detail switches fold into a "(+N more)" counter,
    // so the tool, its input and its output stay visible in a window far too
    // narrow for the real command line. Lossy by design - the point is that a
    // clipped command shows its least interesting half.
    static std::string summarizeCommand(const std::string& command, size_t max_width);

    // summarizeCommand applied to one line of build output: the command lines
    // this class echoes into its own output ("> ...", "Build command: ...") are
    // summarized when they are too long for max_width; every other line, a
    // compiler diagnostic above all, is returned exactly as it came in.
    static std::string summarizeOutputLine(const std::string& line, size_t max_width);

    // -DCMAKE_CXX_COMPILER=... for the configured toolchain, so a project build
    // uses the same compiler as everything else instead of whatever cmake finds.
    // `cxx_override` names a different compiler, for previewing one.
    std::string cmakeToolchainArgs(const std::string& cxx_override = "") const;

    // Human-readable build command preview used by the Build Options dialog.
    // `cxx_override` previews a compiler the user has picked but not accepted;
    // empty means the configured one.
    std::string buildProjectPreview(const GediProject& project, const CompilerSettings& settings,
                                    const std::string& cxx_override = "") const;

private:
    // The compiler's own header search paths (-isystem ...), detected once by
    // asking the configured C++ compiler, so libclang can find <stddef.h>, the
    // C++ standard library, etc. when parsing for highlighting/completion.
    std::vector<std::string> systemIncludeArgs();

    Config m_config;
    std::filesystem::path m_exe_dir;
    std::mutex m_cache_mutex;
    std::map<std::string, std::string> m_compile_command_cache;
    std::vector<std::string> m_sys_include_args;
    bool m_sys_include_cached = false;
};

#endif // BUILDSYSTEM_H
