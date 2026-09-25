// The setup stub.
//
// This executable is what the install builder welds the payload onto, and it is
// also - payload stripped - the uninstaller it leaves behind. Which of the two
// it is doing is decided by what it finds attached to itself.
//
// Getting a graphical UI up before anything is installed takes one trick: SDL2
// lives inside the payload, so it cannot be an ordinary load-time import or the
// process would fail to start. The stub delay-loads it (see the /DELAYLOAD link
// flag), unpacks SDL2.dll and the VGA fonts into a scratch directory first, and
// only then touches anything that calls into SDL.
#include "Install.h"
#include "Payload.h"
#include "Renderer.h"
#include "Wizard.h"
#include "curses_compat.h"

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#endif

using nlohmann::json;

namespace {

// The Borland Classic palette out of gedi's own colors.json, embedded so the
// installer needs no data files to look right.
const char* THEME = R"({
  "ui": {
    "default":                {"fg": "brightyellow", "bg": "blue"},
    "highlight":              {"fg": "black",        "bg": "white"},
    "menu_bar":               {"fg": "black",        "bg": "white"},
    "menu_item":              {"fg": "black",        "bg": "white"},
    "menu_selected":          {"fg": "brightcyan",   "bg": "black"},
    "list_box":               {"fg": "brightcyan",   "bg": "blue"},
    "list_selected":          {"fg": "black",        "bg": "brightcyan"},
    "dialog":                 {"fg": "black",        "bg": "brightwhite"},
    "dialog_title":           {"fg": "brightwhite",  "bg": "blue", "bold": true},
    "selection":              {"fg": "brightwhite",  "bg": "black"},
    "status_bar":             {"fg": "black",        "bg": "brightcyan"},
    "status_bar_highlight":   {"fg": "brightred",    "bg": "brightcyan"},
    "shadow":                 {"fg": "brightblack",  "bg": "black"},
    "changed_indicator":      {"fg": "brightwhite",  "bg": "blue", "bold": true},
    "gutter_bg":              {"fg": "black",        "bg": "brightcyan"},
    "gutter_fg":              {"fg": "brightred",    "bg": "brightcyan"},
    "button_bg":              {"fg": "black",        "bg": "brightcyan"},
    "button_text":            {"fg": "black",        "bg": "brightcyan"},
    "button_hotkey":          {"fg": "brightred",    "bg": "brightcyan"},
    "button_selected_bg":     {"fg": "brightwhite",  "bg": "black"},
    "button_selected_text":   {"fg": "brightwhite",  "bg": "black"},
    "button_selected_hotkey": {"fg": "brightyellow", "bg": "black"},
    "button_shadow":          {"fg": "brightblack",  "bg": "black"},
    "whitespace":             {"fg": "cyan",         "bg": "blue"}
  }
})";

void reportFatal(const std::string& msg) {
#ifdef _WIN32
    MessageBoxW(nullptr, inst::toWide(msg).c_str(), L"Setup", MB_ICONERROR | MB_OK);
#else
    std::fprintf(stderr, "setup: %s\n", msg.c_str());
#endif
}

// The directory holding this executable.
std::string selfDir() {
    const std::string self = payload::Payload::modulePath();
    const size_t slash = self.find_last_of("\\/");
    return (slash == std::string::npos) ? std::string(".") : self.substr(0, slash);
}

} // namespace

int main(int argc, char** argv) {
    bool force_uninstall = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "/uninstall" || a == "--uninstall" || a == "-u") force_uninstall = true;
    }

#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
#endif

    payload::Payload pl;
    std::string err;
    const bool have_payload = pl.open(err);

    // ---- decide which job this is -----------------------------------------
    json manifest;
    const bool uninstalling = force_uninstall || !have_payload;
    if (uninstalling) {
        if (!inst::loadManifest(selfDir(), manifest)) {
            reportFatal(have_payload
                ? "This installer was asked to uninstall, but no installation record was found."
                : "This file carries no setup payload (" + err + ").");
            return 1;
        }
        // The uninstaller sits in the install directory, where SDL2 and the fonts
        // already live, so it only has to look there.
#ifdef _WIN32
        SetDllDirectoryW(inst::toWide(selfDir()).c_str());
        SetCurrentDirectoryW(inst::toWide(selfDir()).c_str());
#endif
    }

    // ---- unpack what the UI itself needs -----------------------------------
    inst::Bootstrap boot;
    if (!uninstalling) {
        std::vector<std::string> names;
        for (const auto& n : pl.spec().value("bootstrap", json::array()))
            names.push_back(n.get<std::string>());
        if (names.empty()) names = { "SDL2.dll", "VGA9.F16" };

        if (!boot.prepare(pl, names, err)) {
            reportFatal("Setup could not start: " + err);
            return 1;
        }
    }

    // ---- the Turbo-style front end ----------------------------------------
    int rc = 0;
    std::string launch_exe, launch_dir;
    {
        Renderer renderer;                       // initscr() + SDL window
        renderer.loadColors(json::parse(THEME));

        const json product = uninstalling ? manifest.value("product", json::object())
                                          : pl.spec().value("product", json::object());
        gui_set_window_title((product.value("name", std::string("Setup")) + " Setup").c_str());
        gui_set_render_mode(2);                  // sharp glyph edges, like gedi's default

        // Setup takes the whole screen, borderless and above everything else -
        // the way a DOS install program simply owned the display.
        gui_set_fullscreen(1);
        renderer.updateDimensions();             // the grid just changed size

        Wizard wiz(renderer, pl);
        if (uninstalling) {
            wiz.runUninstall(manifest);
        } else {
            const bool ok = wiz.run();
            rc = ok ? 0 : 1;

            // Note what to start before the screen goes away; it is actually
            // launched further down, once our own SDL window has closed, so the
            // editor does not come up behind a dying installer.
            if (ok && wiz.launchRequested() && !wiz.launchTarget().empty()) {
                launch_dir = wiz.installDir();
                launch_exe = inst::joinPath(launch_dir, inst::nativePath(wiz.launchTarget()));
            }
        }
        endwin();
    }

    // ---- follow-up actions --------------------------------------------------
#ifdef _WIN32
    if (!uninstalling) {
        // Nothing depends on the UI any more; the scratch directory can go. It
        // has to go before the launch, too - it is still this process's current
        // directory until cleanup() steps out of it.
        boot.cleanup();
    }
    if (!launch_exe.empty() && inst::pathExists(launch_exe)) {
        ShellExecuteW(nullptr, L"open", inst::toWide(launch_exe).c_str(), nullptr,
                      inst::toWide(launch_dir).c_str(), SW_SHOWNORMAL);
    }
    CoUninitialize();
#endif
    return rc;
}
