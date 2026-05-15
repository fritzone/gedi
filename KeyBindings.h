#ifndef KEYBINDINGS_H
#define KEYBINDINGS_H

#include <string>
#include <vector>
#include <map>
#include <array>
#include <ncurses.h>

const int KEY_ALT_OFFSET = 10000;
#define KEY_ALT(c) (KEY_ALT_OFFSET + (c))

enum EditorAction {
    ACT_NEW, ACT_NEW_PROJECT, ACT_ADD_FILE, ACT_OPEN, ACT_SAVE, ACT_SAVE_AS, ACT_EXIT,
    ACT_UNDO, ACT_REDO, ACT_CUT, ACT_COPY, ACT_PASTE, ACT_DELETE,
    ACT_FIND, ACT_REPLACE, ACT_GOTO_LINE, ACT_GO_TO_DEFINITION,
    ACT_COMPILE, ACT_RUN, ACT_COMPILE_OPTIONS, ACT_TOGGLE_OUTPUT,
    ACT_NEXT_BUFFER, ACT_PREV_BUFFER, ACT_CLOSE_BUFFER,
    ACT_SETTINGS, ACT_HELP, ACT_ABOUT, ACT_TOGGLE_COMMENT,
    ACT_UNKNOWN
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
        ActionMapping{ACT_NEW, "new"},
        ActionMapping{ACT_NEW_PROJECT, "new_project"},
        ActionMapping{ACT_OPEN, "open"},
        ActionMapping{ACT_SAVE, "save"},
        ActionMapping{ACT_SAVE_AS, "save_as"},
        ActionMapping{ACT_EXIT, "exit"},
        ActionMapping{ACT_UNDO, "undo"},
        ActionMapping{ACT_REDO, "redo"},
        ActionMapping{ACT_CUT, "cut"},
        ActionMapping{ACT_COPY, "copy"},
        ActionMapping{ACT_PASTE, "paste"},
        ActionMapping{ACT_DELETE, "delete"},
        ActionMapping{ACT_FIND, "find"},
        ActionMapping{ACT_REPLACE, "replace"},
        ActionMapping{ACT_GOTO_LINE, "goto_line"},
        ActionMapping{ACT_COMPILE, "compile"},
        ActionMapping{ACT_RUN, "run"},
        ActionMapping{ACT_COMPILE_OPTIONS, "compile_options"},
        ActionMapping{ACT_TOGGLE_OUTPUT, "toggle_output"},
        ActionMapping{ACT_NEXT_BUFFER, "next_buffer"},
        ActionMapping{ACT_PREV_BUFFER, "prev_buffer"},
        ActionMapping{ACT_CLOSE_BUFFER, "close_buffer"},
        ActionMapping{ACT_SETTINGS, "settings"},
        ActionMapping{ACT_HELP, "help"},
        ActionMapping{ACT_ABOUT, "about"},
        ActionMapping{ACT_TOGGLE_COMMENT, "toggle_comment"},
        ActionMapping{ACT_GO_TO_DEFINITION, "go_to_definition"}
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
    std::map<int, EditorAction> m_keyToAction;
};

#endif // KEYBINDINGS_H
