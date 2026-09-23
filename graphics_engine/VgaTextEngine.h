#pragma once
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <iostream>
#include "Constants.h"

class VgaTextEngine {
public:
    // Path to the VGA font file; defaults to FONT_FILENAME but may be overridden
    // (e.g. by the gedi GUI launcher) so the font can be found regardless of the
    // current working directory.
    std::string font_path = FONT_FILENAME;

    int cursor_x = 1; int cursor_y = 1;
    uint8_t current_fg = LIGHTGRAY; uint8_t current_bg = BLACK;
    int win_x1 = 1; int win_y1 = 1; int win_x2 = 80; int win_y2 = 25;

    // Screen Dimensions
    int SCREEN_COLS = 80;
    int SCREEN_ROWS = 25;

    bool cursor_visible = true;
    bool blink_state = true;
    Uint32 last_blink_time = 0;
    int hw_cursor_x = -1;
    int hw_cursor_y = -1;

    // NEW: Mouse Cursor State
    int mouse_grid_x = -1;
    int mouse_grid_y = -1;
    bool mouse_inside_window = false;

    // Config
    float scale_factor = 1.5f;
    bool smooth_scaling = true;
    bool is_resizable = true;
    bool rounded_corners = false;       // overlay box-drawing glyphs from rounded_font_path
    std::string rounded_font_path;      // e.g. SMVGA.F16 (rounded box/corner glyphs)

    int window_w = 0;
    int window_h = 0;

    std::vector<VGAChar> buffer;
    SDL_Texture* font_texture = nullptr;          // default UI/border/text font (index 0)
    std::vector<SDL_Texture*> font_registry;      // extra fonts; registry id N -> [N-1]
    std::vector<std::string>  font_registry_paths;
    int editor_font_index = 0;                    // registry id used for editor-text cells
    SDL_Renderer* renderer = nullptr;
    SDL_Window* window = nullptr;

    VgaTextEngine();
    void init_sdl();
    void shutdown();
    void render_frame();

    // Core Ops
    void updateGridDims();
    void applyMinimumSize();   // push WM size hints; call only when scale changes, never per-resize
    void handleResize(int w, int h);
    void zoomIn();
    void zoomOut();

    // Set font smoothing (linear vs nearest texture filtering) and rebuild the
    // atlas if it changed. Driven by the "Smooth Text" editor setting.
    void setSmoothScaling(bool on);

    // Use rounded box-drawing glyphs (from rounded_font_path) for the box/corner
    // characters while keeping the main font for everything else. Driven by the
    // "Rounded Corners" editor setting.
    void setRoundedCorners(bool on);

    // Load a font (by path) into the registry and return its id (>=1), reusing the
    // id if already loaded. Empty path returns 0 (the default font). Used both for
    // the editor-text font and for previewing each font in the font picker.
    int registerFont(const std::string& path);

    // Choose the font used for editor-text cells (cells drawn with A_FONT_EDITOR).
    // Empty path = default font. The UI / borders always use the default font.
    void setEditorFont(const std::string& path);

    // Session persistence: read / restore window geometry and zoom.
    void getSessionState(int& win_x, int& win_y, int& win_w, int& win_h, float& scale);
    void applySessionState(int win_x, int win_y, int win_w, int win_h, float scale);

    // Configuration Toggles
    void toggleSmoothScaling();
    void toggleResizable();

    // NEW: Mouse Updates
    void updateMousePos(int x, int y, bool inside);

    // Drawing Primitives
    void gotoxy(int x, int y);
    void textcolor(int color);
    void textbackground(int color);
    void set_window(int x1, int y1, int x2, int y2);
    void clrscr();
    void put_raw(int x, int y, unsigned char c);
    void make_shadow(int x, int y, int w, int h);
    void cprintf(const char* format, ...);

    // Hardware Cursor
    void set_hw_cursor(int x, int y);
    void hide_hw_cursor();

    void pixelToGrid(int px, int py, int* gx, int* gy);

private:
    SDL_Texture* create_font_texture(SDL_Renderer* ren, const unsigned char* font_data);
    SDL_Texture* load_font_file(const std::string& path);   // raw .F16 -> texture (or null)
    void draw_char(unsigned char c, int x, int y, SDL_Color fg, SDL_Color bg, SDL_Texture* tex = nullptr);
    void draw_cursor_block(int x, int y);
    void draw_mouse_overlay(); // NEW: Internal helper
    void reload_font();
};
