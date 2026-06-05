#pragma once
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <SDL2/SDL.h>
#include <functional> // Added for callbacks

class Window {
public:
    int x, y, w, h;
    int restore_x, restore_y, restore_w, restore_h;
    bool is_maximized = false;
    std::string title;
    int id;

    // Drag State
    enum DragMode { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE };
    DragMode currentDragMode = DRAG_NONE;
    int dragStartX = 0, dragStartY = 0;
    int winStartX = 0, winStartY = 0;
    int winStartW = 0, winStartH = 0;

    Window(int _x, int _y, int _w, int _h, const std::string& _title);
    virtual ~Window() {}
    virtual void render(bool isActive, bool allowCursor) = 0;
    virtual void handleInput(SDL_Keycode key, Uint16 mod) = 0;
    virtual void handleTextInput(const char* text) = 0;

    virtual void onResize(int screenCols, int screenRows);
    virtual bool handleMouseClick(int gx, int gy) { return false; }

    void toggleZoom();
    virtual bool onMouseDown(int gx, int gy);      // Used for frame interactions (Move/Resize)
    virtual void onMouseDrag(int gx, int gy);
    virtual void onMouseUp();
};

class EditorWindow : public Window {
public:
    std::vector<std::string> lines;
    std::string fullPath;
    int cursor_x = 0;
    int cursor_y = 0;
    int scroll_x = 0;
    int scroll_y = 0;
    int view_w;
    int view_h;
    bool isModified = false;

    EditorWindow(int _x, int _y, int _w, int _h, const std::string& _title);
    void updateViewSize();
    void saveToFile(const std::string& path);
    void loadFromFile(const std::string& path);
    void scrollToCursor();

    void handleTextInput(const char* text) override;
    void handleInput(SDL_Keycode key, Uint16 mod) override;
    void onResize(int screenCols, int screenRows) override;
    bool handleMouseClick(int gx, int gy) override;

    void render(bool isActive, bool allowCursor) override;
};


class QuestionDialog {
public:
    bool active = false;
    std::string message;

    std::function<void()> onYes;
    std::function<void()> onNo;
    std::function<void()> onSaveFirst;

    int selectedButton = 0; // 0=Yes, 1=No, 2=SaveFirst

    const int W = 45;
    const int H = 10;
    int X, Y; // Calculated in center()

    void open(const std::string& msg, std::function<void()> yesCb, std::function<void()> noCb, std::function<void()> saveCb);
    void center();
    void render();
    void handleInput(SDL_Keycode key, Uint16 mod);
    bool handleMouse(int gx, int gy);
};


class FileDialog {
public:
    // NEW: Generalized Mode
    enum Mode { MODE_OPEN, MODE_SAVE };
    Mode mode = MODE_SAVE;
    std::function<void(std::string)> onComplete; // Callback when OK is clicked

    bool active = false;
    std::string currentPath;
    std::string inputBuffer;
    std::vector<std::filesystem::path> fileList;
    int selectedFileIndex = 0;
    int listScroll = 0;
    enum Focus { FOCUS_INPUT, FOCUS_LIST, FOCUS_OK, FOCUS_CANCEL, FOCUS_HELP };
    Focus currentFocus = FOCUS_INPUT;
    const int W = 50; const int H = 18; int X, Y;

    FileDialog();
    void center();
    void refreshFiles();

    // Updated: Open takes mode and callback
    void open(Mode m, std::function<void(std::string)> callback);
    void render();

    // Updated: No longer needs EditorWindow ptr, uses callback
    void actionOK();
    void handleInput(SDL_Keycode key, Uint16 mod);
    void handleTextInput(const char* text);
};

class WindowManager {
public:
    std::vector<std::shared_ptr<Window>> windows;

    int activeWindowIndex = -1;
    int resize_orig_x, resize_orig_y, resize_orig_w, resize_orig_h;
    std::shared_ptr<FileDialog> fileDialog;
    std::shared_ptr<QuestionDialog> questionDialog; // NEW

    bool isDragging = false;

    WindowManager();
    void addWindow(std::shared_ptr<Window> win);
    void closeActiveWindow();
    void closeAll();
    void nextWindow();
    void prevWindow();
    std::shared_ptr<Window> getActiveWindow();
    void startResize();
    void handleResizeInput(SDL_Keycode key, Uint16 mod);
    void render();

    bool handleMousePress(int gx, int gy);
    bool handleMouse(int gx, int gy);
    void handleMouseDrag(int gx, int gy);
    void handleMouseUp();

    // File Operations
    void requestSave(bool forceDialog, std::function<void()> onSaved = nullptr);

    void requestOpen(); // NEW: Open logic
    void openFile(const std::string& path); // NEW: Helper to create window
    void forceCloseActiveWindow(); // NEW: Internal helper
    void notifyResize(int cols, int rows);
};
