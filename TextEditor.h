#ifndef TEXTEDITOR_H
#define TEXTEDITOR_H

#include "EditorBuffer.h"
#include "Renderer.h"

#include <chrono>
#include <string>
#include <vector>
#include <memory>
#include <future>

#include "SyntaxHighlighter.h"
#include "FileBrowser.h"
#include "ConfigManager.h"
#include "BuildSystem.h"
#include "SearchEngine.h"
#include "HelpProvider.h"
#include "BufferManager.h"
#include "CompletionEngine.h"
#include "debugger/Debugger.h"
#include "MessageDialog.h"
#include <map>
#include <set>
#include "AboutDialog.h"
#include "QuestionDialog.h"
#include "SettingsDialog.h"
#include "ReplaceDialog.h"
#include "GoToLineDialog.h"
#include "CompileOptionsDialog.h"
#include "BuildOutputDialog.h"
#include "HelpDialog.h"
#include "KeyBindings.h"
#include "NewProjectDialog.h"
#include "AddFileDialog.h"
#include "GediProject.h"
#include "ProjectPropertiesDialog.h"

enum MenuAction { CLOSE_MENU, ITEM_SELECTED, NAVIGATE_LEFT, NAVIGATE_RIGHT, RESIZE_OCCURRED };

// Flat entry in the project panel list
struct PanelEntry {
    enum Kind { BUILD_FILE, TARGET_HEADER, SOURCE_FILE } kind;
    std::string display;
    int target_idx = -1;  // index into GediProject::targets
    int source_idx = -1;  // index into ProjectTarget::sources
};

struct ViewState {
    int line_num;
    int col;
    int first_visible_line_num;
};

class TextEditor final {
private:
    static constexpr int PANEL_W = 30;  // project panel width including borders

    bool main_loop_running = true;

    // --- Multi-buffer state ---
    std::unique_ptr<BufferManager> m_bufferManager;

    // --- Global state ---
    std::unique_ptr<Renderer> m_renderer;
    Config m_config;
    std::unique_ptr<ConfigManager> m_configManager;
    std::unique_ptr<BuildSystem> m_buildSystem;
    std::unique_ptr<HelpProvider> m_helpProvider;
    std::unique_ptr<KeyBindings> m_keyBindings;
    std::unique_ptr<CompletionEngine> m_completion;   // libclang code completion
    bool m_batch_input = false;   // true while replaying a paste/escape-seq burst

    // --- Debugger (GDB / future MSVC via dbg::IDebugger) ---
    std::unique_ptr<dbg::IDebugger> m_debugger;
    std::map<std::string, std::set<int>> m_breakpoints;   // abs file path -> line set
    bool        m_debugging      = false;   // a session is loaded
    bool        m_debug_running  = false;   // inferior currently executing
    std::string m_debug_cur_file;           // current stop location (abs path)
    int         m_debug_cur_line = 0;
    std::string m_debug_status;             // shown on the status bar while debugging
    std::vector<dbg::Variable> m_debug_locals;   // refreshed on each stop
    std::vector<dbg::Frame>    m_debug_stack;    // call stack on each stop
    std::vector<std::pair<std::string, std::string>> m_debug_watches;  // expr -> value

    void ToggleBreakpoint();
    void AddWatch();                 // toggle a watch on the identifier under the cursor
    bool startDebugSession();        // build + load + apply breakpoints (no run yet)
    void DebugRunToCursor();         // "Go to Cursor" (F4)

    // Variables window: three focusable/scrollable sections (Locals / Watch / Stack).
    bool m_debug_panel_focused = false;
    int  m_dbg_section   = 0;         // 0 = Locals, 1 = Watch, 2 = Call Stack
    int  m_dbg_cursor[3] = {0, 0, 0};
    int  m_dbg_scroll[3] = {0, 0, 0};
    void FocusDebugPanel();          // give / take keyboard focus to the Variables window
    void handleDebugPanelKey(wint_t ch);
    int  debugSectionCount(int section) const;
    bool promptLine(const std::string& label, std::string& out);  // one-line input
    // Geometry of the Variables window; false when it isn't shown. Fills the outer
    // box (x0,y0,Wp,Hp) and, per section, the title row / first content row / content
    // row count.
    bool debugPanelLayout(int& x0, int& y0, int& Wp, int& Hp,
                          int titleRow[3], int contentY[3], int contentH[3]) const;
    void refreshDebugData();         // pull locals / stack / watch values from backend
    void drawDebugPanel();           // floating "Variables" panel (locals/watch/stack)
    void DebugStartOrContinue();
    void DebugStepOver();
    void DebugStepInto();
    void DebugStepOut();
    void DebugStop();
    void pollDebugEvents();          // drain backend events into the UI (main thread)
    bool lineHasBreakpoint(const std::string& absfile, int line) const;

    // Help
    std::vector<std::string> m_help_history;

    std::vector<std::string> m_clipboard;
    json m_themes_data;
    bool m_search_mode = false;
    std::string m_search_term;
    std::string m_replace_term;
    ViewState m_search_origin;
    int m_search_match_current = 0;
    int m_search_match_total   = 0;

    // Output Screens
    bool m_output_screen_visible = false;
    std::string m_output_content;

    // Graphical run-output pane (captured program output shown in-window).
    std::vector<std::string> m_run_output_lines;
    int  m_run_output_scroll = 0;
    bool m_run_sel_active = false;          // a selection has been made
    bool m_run_selecting  = false;          // mouse button currently held / dragging
    int  m_run_sel_anchor_row = 0, m_run_sel_anchor_col = 0;
    int  m_run_sel_row = 0,        m_run_sel_col = 0;

    // Interactive program execution: the program runs on a pseudo-terminal so it
    // can read stdin and stream stdout/stderr live into the output pane.
    int   m_run_pty_fd  = -1;               // pty master fd (-1 = nothing running)
    long  m_run_pid     = -1;               // child pid
    bool  m_run_running = false;
    bool  m_run_follow  = true;             // auto-scroll to the newest output
    int   m_run_cur_col = 0;                // byte cursor within the live (last) line
    int   m_run_esc     = 0;                // ANSI escape-sequence parse state
    std::string m_run_temp_exe;             // temp executable to delete on exit
    bool m_compile_output_visible = false;
    std::vector<CompileMessage> m_compile_output_lines;
    int m_compile_output_scroll_pos = 0;
    int m_compile_output_cursor_pos = 0;
    ViewState m_pre_compile_view_state;

    // Find All References panel
    bool m_refs_visible = false;
    std::vector<CompileMessage> m_refs_lines;
    int  m_refs_scroll_pos = 0;
    int  m_refs_cursor_pos = 0;
    std::string m_refs_token;

    // Empty-desktop maze (generated once, stable between redraws)
    std::vector<uint8_t> m_desktop_maze;
    int m_desktop_maze_w = 0;
    int m_desktop_maze_h = 0;

    // UI Coordinates
    int m_text_area_start_x = 1, m_text_area_start_y = 2, m_text_area_end_x = 0, m_text_area_end_y = 0;
    int m_gutter_width = 0;

    // Menus
    std::vector<std::string> m_menus;
    std::vector<int> m_menu_positions;

    std::vector<std::string> m_submenu_file;
    std::vector<std::string> m_submenu_edit;
    std::vector<std::string> m_submenu_search;
    std::vector<std::string> m_submenu_build;
    std::vector<std::string> m_submenu_debug;
    std::vector<std::string> m_submenu_project;
    std::vector<std::string> m_submenu_window;
    std::vector<std::string> m_submenu_options;
    std::vector<std::string> m_submenu_help;

    std::filesystem::path m_exe_dir;

public:
    void run(int argc, char* argv[]);

private:
    void updateMenuLabels();
    std::string formatMenuItem(const std::string& label, EditorAction action, int width = 30);
    EditorBuffer& currentBuffer() { return m_bufferManager->currentBuffer(); }
    int currentBufferIdx() const { return m_bufferManager->currentBufferIndex(); }
    void drawEditorState(int active_menu_id = -1);
    void drawEmptyDesktop();
    void drawMainUI();
    void drawTextArea();
    // Map between document character columns and on-screen visual columns,
    // expanding tabs to the configured Tab Size (tab stops). Keeps Show
    // Whitespace, the cursor, mouse hit-testing and scrolling all in agreement.
    int visualColAt(const std::string& text, int char_idx) const;     // char index -> 0-based visual col
    int charColAtVisual(const std::string& text, int target_vcol) const; // visual col -> 1-based char col
    void drawMenuBar(int active_menu_id = -1);
    void drawStatusBar();
    void drawScrollbars();
    void drawCompileOutputWindow();
    void findAllReferences();
    void drawRefsWindow();
    int msgwin_yesno(const std::string& question, const std::string& info);
    void msgwin(const std::string& s);
    void read_file(EditorBuffer& buffer);
    void write_file(EditorBuffer& buffer);
    void main_loop();
    void TryExit();
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
    void addRecentFile(const std::string& path);
    void OpenRecentFile(const std::string& path);
    int  showRecentFilesSubMenu(int x, int y);
    void clearSemanticColors(EditorBuffer& buffer);
    void clearSemanticColorsFrom(EditorBuffer& buffer, int from_line_0based);
    void insertSemanticLine(EditorBuffer& buffer, int split_line_0based);
    void removeSemanticLines(EditorBuffer& buffer, int merge_target_0based, int lines_removed);
    void removeSemanticLine(EditorBuffer& buffer, int removed_line_0based);
    void saveSession();
    void restoreSession();
    void ActivateReplace();
    void PerformReplace();
    bool isAtSearchMatch();
    void GoToLineDialog();
    void GoToDefinition();
    void TriggerCompletion();   // libclang code-completion popup (C/C++ buffers)
    void GoToDiagnostic(bool forward);   // jump to next/previous inline diagnostic
    void GoToNextWord();
    void GoToPreviousWord();
    void GoToNextParagraph();
    void GoToPreviousParagraph();
    void ActivateMenuBar(int initial_menu_id);
    MenuAction CallSubMenu(const std::vector<std::string>& menuItems, int x, int y, int menu_id);
    void CreateNewProject();
    void OpenProject();
    void AddFileToProject();
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
    void ShowOutputScreen();
    // Graphical in-window run-output pane.
    void setRunOutput(const std::string& content);
    void drawRunOutputPane();
    void handleRunOutputKey(wint_t ch);
    void handleRunOutputMouse();
    void copyRunOutputSelection();
    // Interactive (pty-backed) program execution.
    void startRunProgram(const std::string& exe, const std::string& temp_exe);
    void pumpRunProgram();
    void feedRunOutput(const char* data, int n);
    void writeToRunProgram(wint_t ch);
    void finishRunProgram();
    void killRunProgram();
    void CompileOptionsDialog();
    void AboutBox();
    void handleToggleComment();
    void showScrollableOutputDialog(const std::vector<std::string>& lines);
    CompilationResult runCompilationProcess();
    void loadHelpFile();
    void showHelpDialog();
    void NotImplemented() { msgwin("Not Implemented yet."); }
    void ToggleProjectPanel();
    void drawProjectPanel();
    void handleProjectPanelKey(wint_t ch);
    void openProjectPanelFile(int index);
    void CloseProject();
    void openFileAtLine(const std::string& abs_path, int line, int col);
    void ProjectProperties();
    void regenerateBuildFile();
    // Resolve a bundled template file (templates/<name>) against the exe dir,
    // its parents, and the system share dir; returns "" if none is found.
    std::string locateTemplate(const std::string& name) const;
    std::vector<PanelEntry> buildPanelEntries() const;
    void handleMouseEvent();

    // Gutter diagnostic hover: when the mouse rests over a diagnostic line's gutter,
    // a popup lists all its errors/warnings. -1 = no popup.
    void updateDiagnosticHover(int mx, int my);
    void drawDiagnosticHover();
    int  m_diag_hover_line = -1;   // 1-based buffer line whose diagnostics to show
    int  m_diag_hover_mx = 0;      // mouse screen column (popup anchor)
    int  m_diag_hover_my = 0;      // mouse screen row

    // Project panel state
    bool m_project_panel_open    = false;
    bool m_project_panel_focused = false;
    int  m_project_panel_cursor  = 0;
    int  m_project_panel_scroll  = 0;

    // Bracket-match flash: set by handleSmartBlockClose, consumed by drawTextArea
    Line* m_flash_match_line = nullptr;
    int   m_flash_match_col  = -1;
    std::chrono::steady_clock::time_point m_flash_start{};
    static constexpr int FLASH_DURATION_MS = 500;

    // Debounced semantic re-highlight: reset on every keypress, fire after idle
    std::chrono::steady_clock::time_point m_last_keystroke_time{};

    // Mouse state
    bool m_mouse_btn_down        = false;
    int  m_mouse_press_line      = 0;
    int  m_mouse_press_col       = 0;
    int  m_mouse_pending_menu_id = 0;  // set by CallSubMenu when user clicks the menu bar

    // Project-panel double-click detection
    std::chrono::steady_clock::time_point m_panel_last_click_time{};
    int  m_panel_last_click_x = -1;
    int  m_panel_last_click_y = -1;

    // Text-area double-click detection
    std::chrono::steady_clock::time_point m_text_last_click_time{};
    int  m_text_last_click_x = -1;
    int  m_text_last_click_y = -1;

    // Background library scan state
    std::future<std::vector<LibraryInfo>> m_lib_future;
    std::vector<LibraryInfo>              m_cached_libs;
    bool                                  m_libs_cached = false;

    // Currently open project (name.empty() means no project is loaded)
    GediProject m_project;
};


#endif // TEXTEDITOR_H
