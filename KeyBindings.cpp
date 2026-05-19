#include "KeyBindings.h"
#include <algorithm>
#include <cctype>
#include <vector>

#define CTRL(c) ((c) & 0x1f)

KeyBindings::KeyBindings() {
    loadDefaults();
}

void KeyBindings::addBinding(EditorAction action, int key, const std::string& label) {
    m_actionToKeys[action].push_back({key, label});
    if (key != -1) {
        m_keyToAction[key] = action;
    }
}

void KeyBindings::loadDefaults() {
    m_actionToKeys.clear();
    m_keyToAction.clear();

    addBinding(ACT_NEW, CTRL('N'), "Ctrl+N");
    addBinding(ACT_NEW_PROJECT,  -1, "");
    addBinding(ACT_OPEN_PROJECT, -1, "");

    // Multiple bindings for Open
    addBinding(ACT_OPEN, CTRL('O'), "Ctrl+O");
    addBinding(ACT_OPEN, KEY_F(3), "F3");
    
    // Multiple bindings for Save
    addBinding(ACT_SAVE, CTRL('S'), "Ctrl+S");
    addBinding(ACT_SAVE, KEY_F(2), "F2");
    
    addBinding(ACT_SAVE_AS, -1, "");
    addBinding(ACT_EXIT, KEY_ALT('X'), "Alt+X"); 
    addBinding(ACT_UNDO, KEY_ALT(127), "Alt+BS"); 
    addBinding(ACT_REDO, KEY_ALT('Y'), "Alt+Y");
    addBinding(ACT_CUT, CTRL('X'), "Ctrl+X");
    addBinding(ACT_COPY, CTRL('C'), "Ctrl+C");
    addBinding(ACT_PASTE, CTRL('V'), "Ctrl+V");
    addBinding(ACT_DELETE, -1, "");
    addBinding(ACT_FIND, CTRL('F'), "Ctrl+F");
    addBinding(ACT_REPLACE, CTRL('R'), "Ctrl+R");
    addBinding(ACT_GOTO_LINE, -1, "");
    addBinding(ACT_GO_TO_DEFINITION, KEY_F(12), "F12");
    addBinding(ACT_COMPILE, KEY_ALT(KEY_F(9)), "Alt+F9"); 
    addBinding(ACT_RUN, CTRL(KEY_F(9)), "Ctrl+F9"); 
    addBinding(ACT_COMPILE_OPTIONS, -1, "");
    addBinding(ACT_TOGGLE_OUTPUT, KEY_F(5), "F5");
    addBinding(ACT_NEXT_BUFFER, KEY_F(6), "F6");
    addBinding(ACT_PREV_BUFFER, KEY_F(18), "Shift+F6"); 
    addBinding(ACT_CLOSE_BUFFER, KEY_ALT(KEY_F(3)), "Alt+F3");
    addBinding(ACT_SETTINGS, -1, "");
    addBinding(ACT_HELP, KEY_F(1), "F1");
    addBinding(ACT_ABOUT, -1, "");
    addBinding(ACT_TOGGLE_COMMENT, CTRL('/'), "Ctrl+/");
    addBinding(ACT_TOGGLE_PROJECT_PANEL, KEY_ALT('0'), "Alt+0");
    addBinding(ACT_CLOSE_PROJECT, -1, "");
    addBinding(ACT_PROJECT_PROPERTIES, -1, "");
}

int KeyBindings::getKey(EditorAction action) const {
    if (m_actionToKeys.count(action) && !m_actionToKeys.at(action).empty()) {
        return m_actionToKeys.at(action)[0].key;
    }
    return -1;
}

std::string KeyBindings::getLabel(EditorAction action) const {
    if (m_actionToKeys.count(action) && !m_actionToKeys.at(action).empty()) {
        return m_actionToKeys.at(action)[0].label;
    }
    return "";
}

EditorAction KeyBindings::getAction(int key) const {
    if (m_keyToAction.count(key)) return m_keyToAction.at(key);
    return ACT_UNKNOWN;
}

void KeyBindings::loadFromConfig(const std::map<std::string, std::string>& config_map) {
    for (auto const& [action_str, key_str] : config_map) {
        EditorAction act = stringToAction(action_str);
        if (act == ACT_UNKNOWN) continue;
        
        int key = stringToKey(key_str);
        if (key != -1) {
            // In config-driven mode, we'll put the user's key first
            // First, remove standard mappings for this action if we want complete override,
            // or just insert at front. Let's insert at front so user preference is shown in menu.
            m_actionToKeys[act].insert(m_actionToKeys[act].begin(), {key, key_str});
            m_keyToAction[key] = act;
        }
    }
}
std::string_view KeyBindings::actionToString(EditorAction action) {
    auto it = std::find_if(action_map.begin(), action_map.end(),
                           [action](const auto& m) { return m.action == action; });

    return (it != action_map.end()) ? it->name : "unknown";
}

EditorAction KeyBindings::stringToAction(std::string_view str) {
    auto it = std::find_if(action_map.begin(), action_map.end(),
                           [str](const auto& m) { return m.name == str; });

    return (it != action_map.end()) ? it->action : ACT_UNKNOWN;
}

int KeyBindings::stringToKey(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);

    if (s.empty()) return -1;

    // Handle Modifiers
    if (s.find("CTRL+") == 0 && s.length() >= 6) {
        std::string base = s.substr(5);
        if (base == "INS") return 331; 
        if (base.length() == 1) return CTRL(base[0]);
    }
    
    if (s.find("ALT+") == 0 && s.length() >= 5) {
        std::string base = s.substr(4);
        if (base == "X") return KEY_ALT('X');
        if (base == "BS") return KEY_ALT(127);
        if (base == "F9") return KEY_ALT(KEY_F(9));
        if (base == "F5") return KEY_ALT(KEY_F(5));
        if (base == "F3") return KEY_ALT(KEY_F(3));
        if (base.length() == 1) return KEY_ALT(base[0]);
    }

    if (s.find("SHIFT+") == 0 && s.length() >= 7) {
        std::string base = s.substr(6);
        if (base == "DEL") return 330; 
        if (base == "INS") return 337; 
        if (base.find("F") == 0) {
            try { return KEY_F(std::stoi(base.substr(1)) + 12); } catch(...) {}
        }
    }

    // Direct Keys
    if (s.find("F") == 0 && s.length() > 1) {
        try { return KEY_F(std::stoi(s.substr(1))); } catch(...) {}
    }
    
    if (s == "DEL") return KEY_DC;
    if (s == "BS") return KEY_BACKSPACE;
    if (s == "INS") return KEY_IC;
    
    return -1;
}
