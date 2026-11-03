#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <string>

// ═══════════════════════════════════════════════════════════════════════════════
// ProcessRunner - running a build tool without being at its mercy
//
// What popen() could not do for the builds:
//   - hand over the output line by line while the command still runs, so the
//     build window can show it as it comes;
//   - stop the command, and everything it started, when the user cancels;
//   - give it no input: its stdin is the null device, so a tool that wants to
//     ask something gets end-of-file instead of waiting forever for an answer
//     nobody can type;
//   - stop waiting when the command has ended even though something it left
//     running in the background (a compiler server, MSVC's mspdbsrv) still
//     holds the output pipe open;
//   - change the environment for that one command only.
// ═══════════════════════════════════════════════════════════════════════════════
namespace proc {

struct RunResult {
    bool started   = false;   // false: the command could not be started at all
    bool cancelled = false;   // stopped through `cancel`
    int  exit_code = -1;      // the command's exit status (shell semantics)
};

// Runs `command` through the shell (sh -c, or cmd.exe /c on Windows) with
// stdout and stderr merged. Each line of output goes to `on_line` as soon as
// it is complete (without its line break), on the calling thread. While it
// runs, `cancel` is watched: once it is true the command and all its
// descendants are terminated. `env` adds or replaces environment variables
// for this command.
RunResult runShell(const std::string& command,
                   const std::function<void(const std::string&)>& on_line,
                   const std::atomic<bool>* cancel = nullptr,
                   const std::map<std::string, std::string>& env = {});

} // namespace proc
