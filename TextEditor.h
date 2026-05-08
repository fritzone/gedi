#ifndef TEXTEDITOR_H
#define TEXTEDITOR_H

#include "EditorBuffer.h"
#include "Renderer.h"

#include <string>
#include <vector>
#include <memory>

#include "SyntaxHighlighter.h"
#include "FileBrowser.h"
#include "ConfigManager.h"
#include "BuildSystem.h"
#include "SearchEngine.h"
#include "HelpProvider.h"
#include "BufferManager.h"

enum MenuAction { CLOSE_MENU, ITEM_SELECTED, NAVIGATE_LEFT, NAVIGATE_RIGHT, RESIZE_OCCURRED };

struct ViewState {
    int line_num;
    int col;
    int first_visible_line_num;
};

class TextEditor final {
private:
    bool main_loop_running = true;

    // --- Multi-buffer state ---
    std::unique_ptr<BufferManager> m_bufferManager;

    // --- Global state ---
    std::unique_ptr<Renderer> m_renderer;
    Config m_config;
    std::unique_ptr<ConfigManager> m_configManager;
    std::unique_ptr<BuildSystem> m_buildSystem;
    std::unique_ptr<HelpProvider> m_helpProvider;

    // Help
    std::vector<std::string> m_help_history;

    std::vector<std::string> m_clipboard;
    json m_themes_data;
    bool m_search_mode = false;
    std::string m_search_term;
    std::string m_replace_term;
    ViewState m_search_origin;

    // Output Screens
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

    // Menus
    const std::vector<std::string> m_menus = {" &File " , " &Edit " , " &Search ", " &Build " , " &Window ", " &Options ", " &Help "  };

    // Where menus ar drawn
    const std::vector<int> m_menu_positions = {
                1,  // File 2 + 4 = 6
                7,  // Edit
                13, // Search
                21, // Build
                28, // Window
                36, // Options
                45  // Help
    };

    const std::vector<std::string> m_submenu_file = {" &New           Ctrl+N",
                                                     " &Open...       Ctrl+O",
                                                     " -------------- ",
                                                     " &Save          Ctrl+S",
                                                     " Save &As...    ",
                                                     " -------------- ",
                                                     " E&xit          Alt+X"};

    const std::vector<std::string> m_submenu_edit = {" &Undo       Alt+BckSp",
                                                     " &Redo           Alt+Y",
                                                     " -------------- ",
                                                     " Cu&t           Ctrl+X",
                                                     " &Copy          Ctrl+C",
                                                     " &Paste         Ctrl+V",
                                                     " &Delete        ",
                                                     " -------------- ",
                                                     " Comment Line   ",
                                                     " Uncomment Line "};

    const std::vector<std::string> m_submenu_search = {" &Find...       Ctrl+F",
                                                       " Find &Next      ",
                                                       " Find Pre&vious  ",
                                                       " &Replace...    Ctrl+R",
                                                       " -------------- ",
                                                       " &Go To Line... "};

    const std::vector<std::string> m_submenu_build = {" &Run               F9",
                                                      " &Compile     Shift+F9",
                                                      " Compile &Options...  "};

    const std::vector<std::string> m_submenu_window = {" &Output Screen           F5",
                                                       " -------------- ",
                                                       " &Next Window             F6",
                                                       " &Previous Window   Shift+F6",
                                                       " &Close Window        Ctrl+W"};

    const std::vector<std::string> m_submenu_options = {" Editor &Settings... "};

    const std::vector<std::string> m_submenu_help = {" &View Help...  ",
                                                     " &About...      "};

public:
    void run(int argc, char* argv[]);

private:
    EditorBuffer& currentBuffer() { return m_bufferManager->currentBuffer(); }
    int currentBufferIdx() const { return m_bufferManager->currentBufferIndex(); }
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
    void HandleAltKey(wint_t key);
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
    void SwitchToBuffer(int index);
    void compileAndRun();
    void compileOnly();
    void showOutputScreen();
    void CompileOptionsDialog();
    void AboutBox();
    void handleToggleComment();
    void showScrollableOutputDialog(const std::vector<std::string>& lines);
    CompilationResult runCompilationProcess();
    void loadHelpFile();
    void showHelpDialog();
    void NotImplemented() { msgwin("Not Implemented yet."); }

};

#endif // TEXTEDITOR_H
