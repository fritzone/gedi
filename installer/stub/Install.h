#pragma once
#include <functional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "Payload.h"

// Everything the setup stub does to the machine: unpacking files, dropping
// shortcuts, registering with Add/Remove Programs, and undoing all of it again.
//
// Nothing here draws; the wizard passes a progress callback and shows the result.
namespace inst {

// ---- small platform helpers ------------------------------------------------
std::wstring toWide(const std::string& utf8);
std::string  toUtf8(const std::wstring& w);

// Expand %VARIABLES% the way the shell would.
std::string expandEnv(const std::string& s);

// Native separators, no trailing slash.
std::string nativePath(const std::string& p);

// Join with a backslash.
std::string joinPath(const std::string& a, const std::string& b);

// A per-user shell folder: "start_menu" (Programs), "desktop", "temp".
std::string shellFolder(const std::string& which);

bool ensureDir(const std::string& path);
bool pathExists(const std::string& path);
bool isDirWritable(const std::string& path);

// ---- bootstrap -------------------------------------------------------------
// The wizard needs SDL2 and the VGA fonts on disk before it can draw anything,
// but they live inside our own payload. Unpack just those into a scratch
// directory, point the loader and the font search at it, and the graphical UI
// comes up with no external files at all.
//
// This is why the stub delay-loads SDL2.dll: the DLL must not be resolved at
// process start, because at that point it is still inside the payload.
struct Bootstrap {
    std::string dir;                 // scratch directory, removed by cleanup()
    bool prepare(const payload::Payload& pl, const std::vector<std::string>& names, std::string& err);
    void cleanup();
};

// ---- installing ------------------------------------------------------------
struct Plan {
    std::string              dir;             // target directory, already expanded
    std::vector<std::string> components;      // ids the user kept selected
    bool                     start_menu = true;
    bool                     desktop    = false;
    bool                     register_uninstall = true;
};

// name -> "installing <file>", done/total in bytes.
using Progress = std::function<void(const std::string& what, uint64_t done, uint64_t total)>;

struct Result {
    bool        ok = false;
    std::string error;
    uint64_t    bytes_written = 0;
    int         files_written = 0;
};

// Unpack the selected components into plan.dir, create the shortcuts the script
// asks for, write unins.exe plus its manifest, and register the product.
Result install(const payload::Payload& pl, const Plan& plan, const Progress& progress);

// Total unpacked size of the components in `ids`.
uint64_t sizeOf(const payload::Payload& pl, const std::vector<std::string>& ids);

// ---- uninstalling ----------------------------------------------------------
// Read the manifest sitting next to this executable (written at install time).
bool loadManifest(const std::string& dir, nlohmann::json& out);

// Remove every file, shortcut and registry entry the manifest lists.
Result uninstall(const nlohmann::json& manifest, const Progress& progress);

// The manifest file name, and the uninstaller's.
inline constexpr const char* MANIFEST_NAME    = "uninstall.dat";
inline constexpr const char* UNINSTALLER_NAME = "uninstall.exe";

} // namespace inst
