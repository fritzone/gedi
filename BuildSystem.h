#ifndef BUILDSYSTEM_H
#define BUILDSYSTEM_H

#include <atomic>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <set>
#include <functional>
#include "CompilerDefs.h"
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
    bool cancelled = false;  // stopped by the user (success is then false)
};

// Following a build while it runs (the build window): every line of output as
// it is produced, and a flag that stops the build - its tools are killed.
// on_line is called on the thread running the build.
struct BuildMonitor {
    std::function<void(const std::string&)> on_line;
    const std::atomic<bool>* cancel = nullptr;
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

    // Both can run on a thread of their own; `monitor` (optional) streams the
    // output and can cancel.
    CompilationResult runCompilationProcess(EditorBuffer& buffer, const BuildMonitor* monitor = nullptr);
    CompilationResult runProjectBuild(const GediProject& project, const BuildMonitor* monitor = nullptr);
    std::vector<CompileMessage> parseCompilerOutput(const std::string& full_output_str,
                                                    std::vector<std::string>& output_lines_out,
                                                    const std::string& base_dir = "");

    void setConfig(const Config& config);

    // The definition (CompilerDefs.h) of the configured compiler: its command
    // line, option catalogue, error format and capabilities.
    const compilers::Definition& definition() const;
    // The configured C++ compiler executable.
    const std::string& compilerPath() const { return m_config.toolchain.cxx; }
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

    // The flags the settings amount to for the configured compiler.
    std::string settingsFlags(const CompilerSettings& s) const;

    // Try every option of the configured compiler on a one-line program and
    // collect the ones it rejects ("<id>" or "<id>:<choice>"); the result is
    // also remembered for the Compile Options dialog. False (with `error`) when
    // the compiler cannot build the program even without options.
    bool testOptions(std::set<std::string>& unsupported, std::string& error,
                     const std::function<void(int done, int total)>& progress) const;

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
    // For Open Watcom this also selects the "Watcom WMake" generator, the only
    // one CMake drives Watcom's compilers with.
    std::string cmakeToolchainArgs() const;

    // Human-readable build command preview used by the Build Options dialog.
    std::string buildProjectPreview(const GediProject& project, const CompilerSettings& settings) const;

private:
    void resolveDefinition();
    std::string m_def_id;   // the configured compiler's definition, looked up on use

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
