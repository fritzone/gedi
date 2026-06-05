#pragma once
#include <SDL2/SDL.h>
#include <cstdint>

// Font Settings
// We use 'inline' here to allow this header to be included in multiple files safely
inline const char* const FONT_FILENAME = "VGA9.F16";
inline const int FONT_CHAR_WIDTH = 8;
inline const int FONT_CHAR_HEIGHT = 16;
inline const int FONT_NUM_CHARS = 256;
inline const int FONT_BYTES_PER_CHAR = 16;

// Borland Color Constants
#define BLACK         0
#define BLUE          1
#define GREEN         2
#define CYAN          3
#define RED           4
#define MAGENTA       5
#define BROWN         6
#define LIGHTGRAY     7
#define DARKGRAY      8
#define LIGHTBLUE     9
#define LIGHTGREEN    10
#define LIGHTCYAN     11
#define LIGHTRED      12
#define LIGHTMAGENTA  13
#define YELLOW        14
#define WHITE         15

// Menu Constants
enum TopMenu { 
    M_SYSTEM = 0, M_FILE, M_EDIT, M_SEARCH, M_RUN, M_COMPILE, 
    M_DEBUG, M_PROJECT, M_OPTIONS, M_WINDOW, M_HELP, M_COUNT 
};

struct VGAChar {
    unsigned char character_code;
    uint8_t fg_index;
    uint8_t bg_index;
};

// Global Palette
extern const SDL_Color PALETTE[16];