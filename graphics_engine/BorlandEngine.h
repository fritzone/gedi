#pragma once
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <iostream>
#include "Constants.h"

class BorlandEngine {
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

    int window_w = 0;
    int window_h = 0;

    std::vector<VGAChar> buffer;
    SDL_Texture* font_texture = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Window* window = nullptr;

    BorlandEngine();
    void init_sdl();
    void shutdown();
    void render_frame();

    // Core Ops
    void updateGridDims();
    void applyMinimumSize();   // push WM size hints; call only when scale changes, never per-resize
    void handleResize(int w, int h);
    void zoomIn();
    void zoomOut();

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
    void draw_char(unsigned char c, int x, int y, SDL_Color fg, SDL_Color bg);
    void draw_cursor_block(int x, int y);
    void draw_mouse_overlay(); // NEW: Internal helper
    void reload_font();
};
