#ifndef KEYBINDINGS_H
#define KEYBINDINGS_H

#include <string>
#include <vector>
#include <map>
#include <ncurses.h>

const int KEY_ALT_OFFSET = 10000;
#define KEY_ALT(c) (KEY_ALT_OFFSET + (c))

enum EditorAction {
    ACT_NEW, ACT_OPEN, ACT_SAVE, ACT_SAVE_AS, ACT_EXIT,
    ACT_UNDO, ACT_REDO, ACT_CUT, ACT_COPY, ACT_PASTE, ACT_DELETE,
    ACT_FIND, ACT_REPLACE, ACT_GOTO_LINE,
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
    static std::string actionToString(EditorAction action);
    static EditorAction stringToAction(const std::string& str);

private:
    std::map<EditorAction, std::vector<KeyBinding>> m_actionToKeys;
    std::map<int, EditorAction> m_keyToAction;
};

#endif // KEYBINDINGS_H
