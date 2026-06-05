#ifndef KEYBINDINGS_H
#define KEYBINDINGS_H

#include <string>
#include <vector>
#include <map>
#include <array>
#include "curses_compat.h"

const int KEY_ALT_OFFSET = 10000;
#define KEY_ALT(c) (KEY_ALT_OFFSET + (c))

enum class EditorAction {
    // ── File menu ─────────────────────────────────────────────────────────────
    ACT_NEW,               // File > New / Ctrl+N          — new empty buffer
    ACT_NEW_PROJECT,       // File > New Project...         — creates a new .gproj
    ACT_OPEN_PROJECT,      // File > Open Project...        — opens an existing .gproj
    ACT_ADD_FILE,          // Project panel 'A'             — adds a source file to the active target
    ACT_OPEN,              // File > Open / F3 / Ctrl+O    — file browser → load into buffer
    ACT_SAVE,              // File > Save / F2 / Ctrl+S    — saves the current buffer
    ACT_SAVE_AS,           // File > Save As...             — saves with a new filename
    ACT_EXIT,              // File > Exit / Alt+X           — exits the application

    // ── Edit menu ─────────────────────────────────────────────────────────────
    ACT_UNDO,              // Edit > Undo / Alt+BS          — undoes the last edit
    ACT_REDO,              // Edit > Redo / Alt+Y           — redoes the last undone edit
    ACT_CUT,               // Edit > Cut / Ctrl+X           — cuts selection to clipboard
    ACT_COPY,              // Edit > Copy / Ctrl+C          — copies selection to clipboard
    ACT_PASTE,             // Edit > Paste / Ctrl+V         — pastes from clipboard
    ACT_DELETE,            // Edit > Delete                 — deletes the current selection
    ACT_TOGGLE_COMMENT,    // Edit > Comment Line / Ctrl+/  — toggles // comment on current line

    // ── Search menu ───────────────────────────────────────────────────────────
    ACT_FIND,              // Search > Find / Ctrl+F        — opens the search bar
    ACT_REPLACE,           // Search > Replace / Ctrl+R     — opens find/replace dialog
    ACT_FIND_NEXT,         // Search > Find Next            — repeats last search forward
    ACT_FIND_PREV,         // Search > Find Previous        — repeats last search backward
    ACT_GOTO_LINE,         // Search > Go To Line           — opens go-to-line dialog
    ACT_GO_TO_DEFINITION,  // Search > Go To Definition / F12 — jumps to symbol via libclang
    ACT_FIND_REFERENCES,   // Search > Find All References / Shift+F12 — lists every occurrence of token

    // ── Run menu ──────────────────────────────────────────────────────────────
    ACT_COMPILE,           // Run > Compile / Alt+F9        — builds without running
    ACT_RUN,               // Run > Run / Ctrl+F9           — builds and runs the active target
    ACT_COMPILE_OPTIONS,   // Run > Compile Options...      — opens compiler settings dialog

    // ── Window menu ───────────────────────────────────────────────────────────
    ACT_TOGGLE_OUTPUT,     // Window > Output Screen / F5   — shows/hides the build output panel
    ACT_NEXT_BUFFER,       // Window > Next Window / F6     — switches to the next open buffer
    ACT_PREV_BUFFER,       // Window > Previous Window / Shift+F6 — switches to previous buffer
    ACT_CLOSE_BUFFER,      // Window > Close Window / Ctrl+W — closes the active buffer
    ACT_TOGGLE_PROJECT_PANEL, // Window > Project Panel / Alt+0 — shows/hides the project panel

    // ── Options menu ──────────────────────────────────────────────────────────
    ACT_SETTINGS,          // Options > Editor Settings...  — opens the settings dialog

    // ── Help menu ─────────────────────────────────────────────────────────────
    ACT_HELP,              // Help > View Help / F1         — opens the help viewer
    ACT_ABOUT,             // Help > About                  — shows the about box

    // ── Project actions (no menu entry) ───────────────────────────────────────
    ACT_CLOSE_PROJECT,     // File > Close Project          — closes the active project
    ACT_PROJECT_PROPERTIES,// File > Project Properties...  — opens project properties dialog

    // ── Sentinel ──────────────────────────────────────────────────────────────
    ACT_UNKNOWN            // returned by getAction() when no binding matches the key
};

struct KeyBinding {
    int key;
    std::string label;
};

class KeyBindings {
    // A single source of truth for all mappings
    struct ActionMapping {
        EditorAction action;
        std::string_view name;
    };

    static constexpr std::array action_map{
        ActionMapping{EditorAction::ACT_NEW,                "new"},
        ActionMapping{EditorAction::ACT_NEW_PROJECT,        "new_project"},
        ActionMapping{EditorAction::ACT_OPEN_PROJECT,       "open_project"},
        ActionMapping{EditorAction::ACT_OPEN,               "open"},
        ActionMapping{EditorAction::ACT_SAVE,               "save"},
        ActionMapping{EditorAction::ACT_SAVE_AS,            "save_as"},
        ActionMapping{EditorAction::ACT_EXIT,               "exit"},
        ActionMapping{EditorAction::ACT_UNDO,               "undo"},
        ActionMapping{EditorAction::ACT_REDO,               "redo"},
        ActionMapping{EditorAction::ACT_CUT,                "cut"},
        ActionMapping{EditorAction::ACT_COPY,               "copy"},
        ActionMapping{EditorAction::ACT_PASTE,              "paste"},
        ActionMapping{EditorAction::ACT_DELETE,             "delete"},
        ActionMapping{EditorAction::ACT_FIND,               "find"},
        ActionMapping{EditorAction::ACT_REPLACE,            "replace"},
        ActionMapping{EditorAction::ACT_GOTO_LINE,          "goto_line"},
        ActionMapping{EditorAction::ACT_COMPILE,            "compile"},
        ActionMapping{EditorAction::ACT_RUN,                "run"},
        ActionMapping{EditorAction::ACT_COMPILE_OPTIONS,    "compile_options"},
        ActionMapping{EditorAction::ACT_TOGGLE_OUTPUT,      "toggle_output"},
        ActionMapping{EditorAction::ACT_NEXT_BUFFER,        "next_buffer"},
        ActionMapping{EditorAction::ACT_PREV_BUFFER,        "prev_buffer"},
        ActionMapping{EditorAction::ACT_CLOSE_BUFFER,       "close_buffer"},
        ActionMapping{EditorAction::ACT_SETTINGS,           "settings"},
        ActionMapping{EditorAction::ACT_HELP,               "help"},
        ActionMapping{EditorAction::ACT_ABOUT,              "about"},
        ActionMapping{EditorAction::ACT_TOGGLE_COMMENT,     "toggle_comment"},
        ActionMapping{EditorAction::ACT_GO_TO_DEFINITION,   "go_to_definition"},
        ActionMapping{EditorAction::ACT_FIND_REFERENCES,    "find_references"},
        ActionMapping{EditorAction::ACT_TOGGLE_PROJECT_PANEL,"toggle_project_panel"},
        ActionMapping{EditorAction::ACT_CLOSE_PROJECT,      "close_project"},
        ActionMapping{EditorAction::ACT_PROJECT_PROPERTIES, "project_properties"}
    };

public:
    KeyBindings();
    void loadDefaults();
    void loadFromConfig(const std::map<std::string, std::string>& config_map);

    void addBinding(EditorAction action, int key, const std::string& label);
    int getKey(EditorAction action) const;
    std::string getLabel(EditorAction action) const;
    EditorAction getAction(int key) const;

    static int stringToKey(const std::string& str);
    static std::string keyToString(int key);
    static EditorAction stringToAction(std::string_view str);
    static std::string_view actionToString(EditorAction action);
private:
    std::map<EditorAction, std::vector<KeyBinding>> m_actionToKeys;
    std::map<int, EditorAction>                     m_keyToAction;
};

#endif // KEYBINDINGS_H
