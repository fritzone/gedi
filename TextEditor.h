#ifndef TEXTEDITOR_H
#define TEXTEDITOR_H

#include "EditorBuffer.h"
#include "Renderer.h"

#include <string>
#include <vector>
#include <memory>

enum MenuAction { CLOSE_MENU, ITEM_SELECTED, NAVIGATE_LEFT, NAVIGATE_RIGHT, RESIZE_OCCURRED };

struct ViewState {
    int line_num;
    int col;
    int first_visible_line_num;
};

struct SyntaxToken {
    std::string text;
    int colorId;
    int flags = 0;
};

// A struct to hold the results of a compilation
struct CompilationResult {
    std::vector<std::string> output_lines;
    std::string executable_name;
    bool success;
    std::string full_command;
};

// A struct to hold parsed compiler message information
enum CompileMessageType { CMSG_NONE, CMSG_ERROR, CMSG_WARNING, CMSG_NOTE };
struct CompileMessage {
    std::string full_text;
    CompileMessageType type = CMSG_NONE;
    int line = -1;
    int col = -1;
};

class TextEditor {
private:
    bool main_loop_running = true;

    // --- Multi-buffer state ---
    std::vector<EditorBuffer> m_buffers;
    int m_current_buffer_idx = -1;

    // --- Global state ---
    std::unique_ptr<Renderer> m_renderer;
    bool m_smart_indentation = true;
    int m_indentation_width = 4;
    bool m_show_line_numbers = true;

    // --- New Compile Options ---
    int m_compile_mode = -1; // -1: None, 0: Debug, 1: Release
    int m_optimization_level = -1; // -1: None, 0-4 for O0-Os
    std::vector<bool> m_security_flags = {true, true, true, true}; // Corresponds to the security options
    std::string m_extra_compile_flags;

    // Cache for compile commands to avoid re-running cguess.py
    std::map<std::string, std::string> m_compile_command_cache;

    std::vector<std::string> m_clipboard;
    json m_themes_data;
    std::string m_color_scheme_name = "Obsidian";
    bool m_search_mode = false;
    std::string m_search_term;
    std::string m_replace_term;
    ViewState m_search_origin;

    // --- Output Screens ---
    bool m_output_screen_visible = false;
    std::string m_output_content;
    bool m_compile_output_visible = false;
    std::vector<CompileMessage> m_compile_output_lines;
    int m_compile_output_scroll_pos = 0;
    int m_compile_output_cursor_pos = 0;
    ViewState m_pre_compile_view_state;

    // UI Coordinates
    int m_text_area_start_x = 1, m_text_area_start_y = 2, m_text_area_end_x = 0, m_text_area_end_y = 0;
    int m_gutter_width = 0;

    // --- Menus ---
    const std::vector<std::string> m_menus = {" &File " , " &Edit " , " &Search ", " &Build " , " &Window ", " &Options ", " &Help "  };
    const std::vector<int> m_menu_positions = {2, 9, 17, 26, 34, 43, 53};

    const std::vector<std::string> m_submenu_file = {" &New           Ctrl+N", " &Open...       Ctrl+O", " -------------- ", " &Save          Ctrl+S", " Save &As...    ", " -------------- ", " E&xit          Alt+X"};
    const std::vector<std::string> m_submenu_edit = {" &Undo       Alt+BckSp", " &Redo           Alt+Y", " -------------- ", " Cu&t           Ctrl+X", " &Copy          Ctrl+C", " &Paste         Ctrl+V", " &Delete        ", " -------------- ", " Comment Line   ", " Uncomment Line "};
    const std::vector<std::string> m_submenu_search = {" &Find...       Ctrl+F", " Find &Next      ", " Find Pre&vious ", " &Replace...    Ctrl+R", " -------------- ", " &Go To Line... "};
    const std::vector<std::string> m_submenu_build = {" &Run               F9", " &Compile       S-F9", " Compile &Options..."};
    const std::vector<std::string> m_submenu_window = {" &Output Screen       F5", " -------------- ", " &Next Window         F6", " &Previous Window  S-F6", " &Close Window     Alt+W"};
    const std::vector<std::string> m_submenu_options = {" Editor &Settings... "};
    const std::vector<std::string> m_submenu_help = {" &View Help...  ", " &About...      "};

public:
    void run(int argc, char* argv[]);

private:
    EditorBuffer& currentBuffer() { return m_buffers[m_current_buffer_idx]; }

    void drawEditorState(int active_menu_id = -1);
    void drawMainUI();
    void drawTextArea();
    void drawMenuBar(int active_menu_id = -1);
    void drawStatusBar();
    void drawScrollbars();
    void drawCompileOutputWindow();
    int msgwin_yesno(const std::string& question, const std::string& filename_in);
    void msgwin(const std::string& s);
    void read_file(EditorBuffer& buffer);
    void write_file(EditorBuffer& buffer);
    void main_loop();
    void insert_line_after(EditorBuffer& buffer, Line* current_p, const std::string& s);
    void process_key(wint_t ch);
    void handle_alt_key(wint_t key);
    void handleSmartBlockClose(wint_t closing_char);
    void update_cursor_and_scroll();
    void handleResize();
    void ClearSelection();
    void UpdateSelection();
    void DeleteSelection();
    void HandleCopy();
    void HandleCut();
    void HandlePaste();
    void CreateUndoPoint(EditorBuffer& buffer);
    void HandleUndo();
    void HandleRedo();
    void RestoreStateFromRecord(EditorBuffer& buffer, const UndoRecord& record);
    void ActivateSearch();
    void DeactivateSearch();
    void PerformSearch(bool next);
    void ActivateReplace();
    void PerformReplace();
    void PerformReplaceAll();
    void GoToLineDialog();
    void GoToNextWord();
    void GoToPreviousWord();
    void GoToNextParagraph();
    void GoToPreviousParagraph();
    void ActivateMenuBar(int initial_menu_id);
    MenuAction CallSubMenu(const std::vector<std::string>& menuItems, int x, int y, int menu_id);
    void DoNew();
    void selectfile();
    void OpenFileBrowser();
    void SaveFileBrowser();
    void EditorSettingsDialog();
    void NextWindow();
    void PreviousWindow();
    void CloseWindow();
    void loadConfig();
    void saveConfig();
    void createDefaultConfigFile();
    void SwitchToBuffer(int index);
    void compileAndRun();
    void compileOnly();
    void showOutputScreen();
    void CompileOptionsDialog();
    void setSyntaxType(EditorBuffer& buffer);
    void loadKeywords(EditorBuffer& buffer);
    std::vector<SyntaxToken> parseLine(EditorBuffer& buffer, const std::string& line);
    void noti() { msgwin("Not Implemented yet."); }
    void about_box() { msgwin("gedi C++ Editor"); }
    void handleToggleComment();
    void showScrollableOutputDialog(const std::vector<std::string>& lines);
    CompilationResult runCompilationProcess();
};

#endif // TEXTEDITOR_H
