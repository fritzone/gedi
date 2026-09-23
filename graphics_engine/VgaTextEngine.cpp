#include "VgaTextEngine.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <algorithm>
#ifdef GEDI_HAVE_SDL_IMAGE
#include <SDL2/SDL_image.h>
#endif

const SDL_Color PALETTE[16] = {
    {0x00, 0x00, 0x00, 0xFF}, {0x00, 0x00, 0xA8, 0xFF}, {0x00, 0xA8, 0x00, 0xFF}, {0x00, 0xA8, 0xA8, 0xFF},
    {0xA8, 0x00, 0x00, 0xFF}, {0xA8, 0x00, 0xA8, 0xFF}, {0xA8, 0x54, 0x00, 0xFF}, {0xA8, 0xA8, 0xA8, 0xFF},
    {0x54, 0x54, 0x54, 0xFF}, {0x54, 0x54, 0xFC, 0xFF}, {0x54, 0xFC, 0x54, 0xFF}, {0x54, 0xFC, 0xFC, 0xFF},
    {0xFC, 0x54, 0x54, 0xFF}, {0xFC, 0x54, 0xFC, 0xFF}, {0xFC, 0xFC, 0x54, 0xFF}, {0xFC, 0xFC, 0xFC, 0xFF}
};

VgaTextEngine::VgaTextEngine() {
    buffer.resize(SCREEN_COLS * SCREEN_ROWS, { ' ', LIGHTGRAY, BLACK });
}

void VgaTextEngine::init_sdl() {
    SDL_Init(SDL_INIT_VIDEO);

    window_w = 80 * FONT_CHAR_WIDTH * scale_factor;
    window_h = 25 * FONT_CHAR_HEIGHT * scale_factor;

    window = SDL_CreateWindow("Garland Turbine C++", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              window_w, window_h, SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // NEW: Hide the OS hardware cursor
    SDL_ShowCursor(SDL_DISABLE);

    reload_font();
    updateGridDims();
    applyMinimumSize();
}

void VgaTextEngine::reload_font() {
    if (font_texture) {
        SDL_DestroyTexture(font_texture);
        font_texture = nullptr;
    }
    for (SDL_Texture* t : font_registry) if (t) SDL_DestroyTexture(t);
    font_registry.clear();
    // Derive the atlas rasterisation parameters + texture filtering from the mode.
    //   PIXELATED: nearest filter, 1x binary atlas.
    //   SMOOTH:    linear filter, 1x binary atlas (wide, soft antialiasing).
    //   SHARP:     linear filter, supersampled coverage atlas (crisp antialiasing).
    // SHARP uses a 3x supersample: the glyph strip stays within the 8192px texture
    // width every GPU supports ((8*3+2)*256 = 6656) while giving a ~1/3-pixel edge.
    switch (render_mode) {
        case RENDER_PIXELATED:
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   // nearest
            atlas_ss_ = 1; atlas_aa_ = false;
            break;
        case RENDER_SHARP:
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");   // linear
            atlas_ss_ = 3; atlas_aa_ = true;
            break;
        case RENDER_SMOOTH:
        default:
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");   // linear
            atlas_ss_ = 1; atlas_aa_ = false;
            break;
    }
    unsigned char font_data[FONT_NUM_CHARS * FONT_BYTES_PER_CHAR];
    FILE* font_file = fopen(font_path.c_str(), "rb");
    if (!font_file) { std::cerr << "Error: " << font_path << " missing." << std::endl; exit(1); }
    size_t rd = fread(font_data, 1, FONT_NUM_CHARS * FONT_BYTES_PER_CHAR, font_file);
    (void)rd;
    fclose(font_file);

    // Rounded corners: overlay the CP437 box-drawing glyphs (0xB3..0xDA) from the
    // rounded font, keeping the main font for every other character.
    if (rounded_corners && !rounded_font_path.empty()) {
        if (FILE* rf = fopen(rounded_font_path.c_str(), "rb")) {
            unsigned char rdata[FONT_NUM_CHARS * FONT_BYTES_PER_CHAR];
            size_t got = fread(rdata, 1, sizeof(rdata), rf);
            fclose(rf);
            if (got == sizeof(rdata)) {
                for (int slot = 0xB3; slot <= 0xDA; ++slot)
                    memcpy(&font_data[slot * FONT_BYTES_PER_CHAR],
                           &rdata[slot * FONT_BYTES_PER_CHAR], FONT_BYTES_PER_CHAR);
            }
        }
    }
    font_texture = create_font_texture(renderer, font_data);

    // Rebuild every registered font with the current scale-quality hint.
    for (const std::string& p : font_registry_paths)
        font_registry.push_back(load_font_file(p));
}

// Read a raw 8x16 .F16 (4096 bytes) and build its glyph-atlas texture.
SDL_Texture* VgaTextEngine::load_font_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    unsigned char data[FONT_NUM_CHARS * FONT_BYTES_PER_CHAR];
    size_t got = fread(data, 1, sizeof(data), f);
    fclose(f);
    if (got != sizeof(data)) return nullptr;
    return create_font_texture(renderer, data);
}

int VgaTextEngine::registerFont(const std::string& path) {
    if (path.empty()) return 0;   // default font
    for (size_t i = 0; i < font_registry_paths.size(); ++i)
        if (font_registry_paths[i] == path) return (int)i + 1;
    font_registry_paths.push_back(path);
    font_registry.push_back(load_font_file(path));
    return (int)font_registry_paths.size();
}

void VgaTextEngine::setEditorFont(const std::string& path) {
    editor_font_index = registerFont(path);
}

void VgaTextEngine::setRoundedCorners(bool on) {
    if (on == rounded_corners) return;
    rounded_corners = on;
    reload_font();
}

void VgaTextEngine::cycleRenderMode() {
    render_mode = (render_mode + 1) % RENDER_MODE_COUNT;
    reload_font();
}

const char* VgaTextEngine::renderModeName(int mode) {
    switch (mode) {
        case RENDER_PIXELATED: return "Pixelated";
        case RENDER_SHARP:     return "Sharp";
        case RENDER_SMOOTH:    return "Smooth";
        default:               return "Smooth";
    }
}

void VgaTextEngine::toggleResizable() {
    is_resizable = !is_resizable;
    SDL_SetWindowResizable(window, is_resizable ? SDL_TRUE : SDL_FALSE);
}

// NEW: Update mouse state
void VgaTextEngine::updateMousePos(int x, int y, bool inside) {
    mouse_grid_x = x;
    mouse_grid_y = y;
    mouse_inside_window = inside;
}

// Push the 80x25 floor to the window manager as WM size hints.  This MUST NOT be
// called from the per-resize path: re-sending WM_NORMAL_HINTS while the WM holds
// the interactive resize grip confuses it about which edge is anchored, which
// makes the opposite edge jump (e.g. a left-edge drag flinging the window to full
// height).  The minimum only depends on scale_factor, so it is set once at startup
// and again whenever the zoom level changes.
void VgaTextEngine::applyMinimumSize() {
    int min_w = (int)(80 * FONT_CHAR_WIDTH * scale_factor);
    int min_h = (int)(25 * FONT_CHAR_HEIGHT * scale_factor);
    SDL_SetWindowMinimumSize(window, min_w, min_h);
}

void VgaTextEngine::updateGridDims() {
    int logical_w = (int)(window_w / scale_factor);
    int logical_h = (int)(window_h / scale_factor);
    int cols = logical_w / FONT_CHAR_WIDTH;
    int rows = logical_h / FONT_CHAR_HEIGHT;

    if (cols < 80) cols = 80;
    if (rows < 25) rows = 25;

    SCREEN_COLS = cols;
    SCREEN_ROWS = rows;
    win_x1 = 1; win_y1 = 1; win_x2 = SCREEN_COLS; win_y2 = SCREEN_ROWS;
    buffer.resize(SCREEN_COLS * SCREEN_ROWS, { ' ', LIGHTGRAY, BLACK });
    SDL_RenderSetScale(renderer, scale_factor, scale_factor);
}

void VgaTextEngine::handleResize(int w, int h) {
    // Do NOT call SDL_SetWindowSize() here.  This runs in response to a user /
    // window-manager driven resize (SDL_WINDOWEVENT_RESIZED), which means the WM
    // still owns the resize grip.  Calling SDL_SetWindowSize() back at the WM
    // mid-drag makes the two fight over the size, so the window oscillates and
    // jumps around uncontrollably.  The 80x25 floor is already enforced by
    // SDL_SetWindowMinimumSize() (set once in applyMinimumSize), and updateGridDims
    // also clamps the grid to >=80x25, so simply accepting whatever size the WM
    // gives us is safe.
    window_w = w; window_h = h;
    updateGridDims();
}

void VgaTextEngine::zoomIn() {
    float next_scale = scale_factor + 0.25f;
    scale_factor = next_scale;
    int min_w = (int)(80 * FONT_CHAR_WIDTH * scale_factor);
    int min_h = (int)(25 * FONT_CHAR_HEIGHT * scale_factor);
    if (window_w < min_w) window_w = min_w;
    if (window_h < min_h) window_h = min_h;
    SDL_SetWindowSize(window, window_w, window_h);
    updateGridDims();
    applyMinimumSize();
}

void VgaTextEngine::zoomOut() {
    if (scale_factor > 0.5f) {
        scale_factor -= 0.25f;
        updateGridDims();
        applyMinimumSize();
    }
}

void VgaTextEngine::setRenderMode(int mode) {
    if (mode < 0 || mode >= RENDER_MODE_COUNT) mode = RENDER_SMOOTH;
    if (mode == render_mode) return;
    render_mode = mode;
    reload_font();   // re-applies SDL_HINT_RENDER_SCALE_QUALITY + rebuilds the atlas
}

void VgaTextEngine::getSessionState(int& win_x, int& win_y, int& win_w, int& win_h, float& scale) {
    win_x = 0; win_y = 0; win_w = window_w; win_h = window_h;
    if (window) {
        SDL_GetWindowPosition(window, &win_x, &win_y);
        SDL_GetWindowSize(window, &win_w, &win_h);
    }
    scale = scale_factor;
}

void VgaTextEngine::applySessionState(int win_x, int win_y, int win_w, int win_h, float scale) {
    if (scale < 0.5f) scale = 0.5f;
    scale_factor = scale;

    // Honour the 80x25 floor for the restored zoom level.
    int min_w = (int)(80 * FONT_CHAR_WIDTH * scale_factor);
    int min_h = (int)(25 * FONT_CHAR_HEIGHT * scale_factor);
    if (win_w < min_w) win_w = min_w;
    if (win_h < min_h) win_h = min_h;
    window_w = win_w; window_h = win_h;

    if (window) {
        SDL_SetWindowSize(window, window_w, window_h);
        SDL_SetWindowPosition(window, win_x, win_y);
    }
    updateGridDims();
    applyMinimumSize();
}

bool VgaTextEngine::setImageOverlay(const std::string& path, int cx, int cy, int cw, int ch,
                                    bool tint, SDL_Color body, SDL_Color accent) {
    clearImageOverlay();
    if (!renderer || path.empty()) return false;

    SDL_Surface* raw = nullptr;
#ifdef GEDI_HAVE_SDL_IMAGE
    raw = IMG_Load(path.c_str());
#endif
    // Fall back to SDL's built-in BMP loader (always available) so a .bmp still
    // works even in a build without SDL_image.
    if (!raw) raw = SDL_LoadBMP(path.c_str());
    if (!raw) return false;

    const bool src_has_alpha = (raw->format->Amask != 0);
    SDL_Surface* surf = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(raw);
    if (!surf) return false;

    int img_w = surf->w, img_h = surf->h;

    if (tint && img_w > 0 && img_h > 0) {
        // Recolour the logo to the active scheme. Each opaque pixel keeps its own
        // luminance (so the metallic shading survives) but is mapped onto a ramp of
        // the scheme's colour: a dark shade at the bottom, the colour itself in the
        // mids, and a white sheen at the highlights. Blue-dominant pixels (the "++")
        // use the accent colour; everything else uses the body colour.
        SDL_LockSurface(surf);
        Uint32* px = (Uint32*)surf->pixels;
        const int stride = surf->pitch / 4;
        auto ramp = [](float base, float L, float sheen) {
            float lo = base * 0.20f;
            float v  = lo + (base - lo) * L;
            return v + (255.0f - v) * sheen;
        };
        for (int y = 0; y < img_h; ++y) {
            for (int x = 0; x < img_w; ++x) {
                Uint32& p = px[y * stride + x];
                Uint8 r, g, b, a; SDL_GetRGBA(p, surf->format, &r, &g, &b, &a);
                if (a == 0) continue;   // leave fully-transparent pixels alone
                float L = (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
                float sheen = (L - 0.82f) / 0.18f;
                sheen = (sheen < 0 ? 0 : (sheen > 1 ? 1 : sheen)) * 0.5f;
                bool is_accent = (int)b - (int)std::max(r, g) > 38;
                const SDL_Color& c = is_accent ? accent : body;
                Uint8 nr = (Uint8)(ramp(c.r, L, sheen) + 0.5f);
                Uint8 ng = (Uint8)(ramp(c.g, L, sheen) + 0.5f);
                Uint8 nb = (Uint8)(ramp(c.b, L, sheen) + 0.5f);
                p = SDL_MapRGBA(surf->format, nr, ng, nb, a);
            }
        }
        SDL_UnlockSurface(surf);
    }
    // A logo shipped as opaque RGB carries a flat background that would otherwise
    // draw as a solid rectangle on the dialog. When the source has no alpha of its
    // own (and we are not tinting), key that background (sampled from the four
    // corners) out to transparent, with a short distance ramp so edges stay smooth.
    else if (!src_has_alpha && img_w > 0 && img_h > 0) {
        SDL_LockSurface(surf);
        Uint32* px = (Uint32*)surf->pixels;
        const int stride = surf->pitch / 4;
        auto getrgb = [&](Uint32 p, int& r, int& g, int& b) {
            Uint8 rr, gg, bb, aa; SDL_GetRGBA(p, surf->format, &rr, &gg, &bb, &aa);
            r = rr; g = gg; b = bb;
        };
        int r0, g0, b0, r1, g1, b1, r2, g2, b2, r3, g3, b3;
        getrgb(px[0],                               r0, g0, b0);
        getrgb(px[img_w - 1],                       r1, g1, b1);
        getrgb(px[(img_h - 1) * stride],            r2, g2, b2);
        getrgb(px[(img_h - 1) * stride + img_w - 1],r3, g3, b3);
        const float br = (r0 + r1 + r2 + r3) / 4.0f;
        const float bg = (g0 + g1 + g2 + g3) / 4.0f;
        const float bb = (b0 + b1 + b2 + b3) / 4.0f;
        const float T0 = 18.0f, T1 = 32.0f;   // key <=T0 fully out, >=T1 fully opaque
        for (int y = 0; y < img_h; ++y) {
            for (int x = 0; x < img_w; ++x) {
                Uint32& p = px[y * stride + x];
                int r, g, b; getrgb(p, r, g, b);
                float d = std::sqrt((r - br) * (r - br) + (g - bg) * (g - bg) + (b - bb) * (b - bb));
                Uint8 a = d <= T0 ? 0
                        : d >= T1 ? 255
                        : (Uint8)(255.0f * (d - T0) / (T1 - T0) + 0.5f);
                p = SDL_MapRGBA(surf->format, (Uint8)r, (Uint8)g, (Uint8)b, a);
            }
        }
        SDL_UnlockSurface(surf);
    }

    overlay_tex_ = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (!overlay_tex_) return false;
    SDL_SetTextureBlendMode(overlay_tex_, SDL_BLENDMODE_BLEND);

    // Fit the image inside the cell rectangle (in logical pixels) preserving its
    // aspect ratio, then centre it there.
    int box_x = cx * FONT_CHAR_WIDTH;
    int box_y = cy * FONT_CHAR_HEIGHT;
    int box_w = cw * FONT_CHAR_WIDTH;
    int box_h = ch * FONT_CHAR_HEIGHT;
    if (img_w <= 0 || img_h <= 0) { clearImageOverlay(); return false; }

    float s = std::min((float)box_w / img_w, (float)box_h / img_h);
    if (s > 1.0f) s = 1.0f;                       // never upscale past native size
    int dst_w = (int)(img_w * s + 0.5f);
    int dst_h = (int)(img_h * s + 0.5f);
    overlay_dst_ = { box_x + (box_w - dst_w) / 2, box_y + (box_h - dst_h) / 2, dst_w, dst_h };
    return true;
}

void VgaTextEngine::clearImageOverlay() {
    if (overlay_tex_) { SDL_DestroyTexture(overlay_tex_); overlay_tex_ = nullptr; }
    overlay_dst_ = { 0, 0, 0, 0 };
}

void VgaTextEngine::shutdown() {
    clearImageOverlay();
    SDL_DestroyTexture(font_texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
}

void VgaTextEngine::gotoxy(int x, int y) { cursor_x = x; cursor_y = y; }
void VgaTextEngine::textcolor(int color) { current_fg = color % 16; }
void VgaTextEngine::textbackground(int color) { current_bg = color % 16; }
void VgaTextEngine::set_window(int x1, int y1, int x2, int y2) {
    win_x1 = x1; win_y1 = y1; win_x2 = x2; win_y2 = y2;
    cursor_x = 1; cursor_y = 1;
}

void VgaTextEngine::clrscr() {
    for (int y = win_y1; y <= win_y2; ++y)
        for (int x = win_x1; x <= win_x2; ++x) put_raw(x, y, ' ');
    cursor_x = 1; cursor_y = 1;
}

void VgaTextEngine::put_raw(int x, int y, unsigned char c) {
    if (x < 1 || x > SCREEN_COLS || y < 1 || y > SCREEN_ROWS) return;
    int idx = (y-1)*SCREEN_COLS + (x-1);
    if(idx < buffer.size()) buffer[idx] = { c, current_fg, current_bg };
}

void VgaTextEngine::make_shadow(int x, int y, int w, int h) {
    for(int cy = y; cy < y + h; cy++) {
        for(int cx = x; cx < x + w; cx++) {
            if (cx < 1 || cx > SCREEN_COLS || cy < 1 || cy > SCREEN_ROWS) continue;
            int idx = (cy - 1) * SCREEN_COLS + (cx - 1);
            if(idx < buffer.size()) {
                buffer[idx].fg_index = DARKGRAY;
                buffer[idx].bg_index = BLACK;
            }
        }
    }
}

void VgaTextEngine::cprintf(const char* format, ...) {
    char buffer[256];
    va_list args; va_start(args, format); vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    std::string s = buffer;
    for (char c : s) {
        int abs_x = win_x1 + cursor_x - 1; int abs_y = win_y1 + cursor_y - 1;
        if (abs_x >= win_x1 && abs_x <= win_x2 && abs_y >= win_y1 && abs_y <= win_y2) put_raw(abs_x, abs_y, (unsigned char)c);
        cursor_x++;
        if (cursor_x > (win_x2 - win_x1) + 1) { cursor_x = 1; cursor_y++; }
    }
}

void VgaTextEngine::set_hw_cursor(int x, int y) {
    int abs_x = win_x1 + x - 1; int abs_y = win_y1 + y - 1;
    if (abs_x >= win_x1 && abs_x <= win_x2 && abs_y >= win_y1 && abs_y <= win_y2) {
        hw_cursor_x = abs_x; hw_cursor_y = abs_y;
    } else { hw_cursor_x = -1; }
}

void VgaTextEngine::hide_hw_cursor() { hw_cursor_x = -1; }

void VgaTextEngine::render_frame() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    for (int y = 0; y < SCREEN_ROWS; ++y) {
        for (int x = 0; x < SCREEN_COLS; ++x) {
            int idx = y * SCREEN_COLS + x;
            if(idx < buffer.size()) {
                VGAChar v = buffer[idx];
                // font_index: 0 = default; 63 = the editor-text font; 1..62 = a
                // registered font (used to preview each font in the font picker).
                int fi = v.font_index;
                if (fi == 63) fi = editor_font_index;
                SDL_Texture* tex = font_texture;
                if (fi >= 1 && fi <= (int)font_registry.size() && font_registry[fi - 1])
                    tex = font_registry[fi - 1];
                draw_char(v.character_code, x, y, PALETTE[v.fg_index], PALETTE[v.bg_index], tex);
            }
        }
    }

    // Draw the About-box image (if any) on top of the text it was laid over.
    if (overlay_tex_)
        SDL_RenderCopy(renderer, overlay_tex_, nullptr, &overlay_dst_);

    // NEW: Draw the text-mode mouse cursor on top
    if (mouse_inside_window) {
        draw_mouse_overlay();
    }

    Uint32 now = SDL_GetTicks();
    if (now - last_blink_time > 500) { blink_state = !blink_state; last_blink_time = now; }
    if (cursor_visible && blink_state && hw_cursor_x != -1) {
        draw_cursor_block(hw_cursor_x - 1, hw_cursor_y - 1);
    }
    SDL_RenderPresent(renderer);
}

// NEW: Helper to draw the red mouse block
void VgaTextEngine::draw_mouse_overlay() {
    if (mouse_grid_x < 1 || mouse_grid_x > SCREEN_COLS || mouse_grid_y < 1 || mouse_grid_y > SCREEN_ROWS) return;

    int idx = (mouse_grid_y - 1) * SCREEN_COLS + (mouse_grid_x - 1);
    if (idx < 0 || idx >= buffer.size()) return;

    VGAChar c = buffer[idx];

    // Calculate color: Red Background. Keep foreground unless it was also red.
    SDL_Color bg = PALETTE[RED];
    SDL_Color fg = PALETTE[c.fg_index];

    // If the text was RED, we must change it to WHITE or BLACK to be visible
    if (c.fg_index == RED || c.fg_index == LIGHTRED) {
        fg = PALETTE[WHITE];
    }

    // Draw the character over the existing one
    draw_char(c.character_code, mouse_grid_x - 1, mouse_grid_y - 1, fg, bg);
}

SDL_Texture* VgaTextEngine::create_font_texture(SDL_Renderer* ren, const unsigned char* font_data) {
    // Each glyph occupies a padded cell. The glyph itself sits at (PAD,PAD); the
    // surrounding gutter replicates the glyph's edge pixels so that linear
    // filtering at the glyph boundary samples the glyph's own edge rather than
    // bleeding in the neighbouring glyph from the atlas.
    //
    // In SHARP mode the glyph is rasterised at atlas_ss_x its native size with a
    // real antialiased coverage in the alpha channel (see below); otherwise it is
    // a 1x binary mask (alpha 0 or 255), as the classic pixelated/smooth modes use.
    const int SS     = atlas_ss_;
    const int PAD    = FONT_ATLAS_PAD;
    const int glyph_w = FONT_CHAR_WIDTH  * SS;
    const int glyph_h = FONT_CHAR_HEIGHT * SS;
    const int cell_w = glyph_w + 2 * PAD;            // atlas stride per glyph
    const int cell_h = glyph_h + 2 * PAD;
    const int atlas_w = cell_w * FONT_NUM_CHARS;

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, atlas_w, cell_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_LockSurface(surface);
    Uint32* pixels = (Uint32*)surface->pixels;
    // Glyphs are always white; the foreground colour is applied at draw time via
    // SDL_SetTextureColorMod. Only the alpha (coverage) varies per texel.
    const Uint32 white_rgb = SDL_MapRGBA(surface->format, 255, 255, 255, 0);
    const Uint8  Ashift    = surface->format->Ashift;

    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };

    // One source glyph pixel (edge-clamped so the gutter replicates the border).
    auto bit = [&](const unsigned char* g, int r, int c) -> float {
        r = clampi(r, 0, FONT_CHAR_HEIGHT - 1);
        c = clampi(c, 0, FONT_CHAR_WIDTH  - 1);
        return ((g[r] >> (7 - c)) & 1) ? 1.0f : 0.0f;
    };

    // SHARP antialiasing: bilinearly sample the 1-bit glyph in source-pixel space
    // (a coverage that ramps linearly across one source pixel at every edge), then
    // push it through a steep smoothstep so the ramp collapses to ~1 atlas texel.
    // That keeps the edge smooth (no jaggies) but crisp and high-contrast, instead
    // of the whole-source-pixel-wide fuzz plain linear filtering of a binary mask
    // produces. edge = 0.5/SS makes the antialiased band about one atlas texel wide
    // regardless of the supersample factor.
    const float edge = 0.5f / (float)SS;

    for (int i = 0; i < FONT_NUM_CHARS; ++i) {
        const unsigned char* glyph = font_data + i * FONT_BYTES_PER_CHAR;
        int cell_x = i * cell_w;
        for (int ay = 0; ay < cell_h; ++ay) {
            for (int ax = 0; ax < cell_w; ++ax) {
                float cov;
                if (!atlas_aa_) {
                    // Binary mask: nearest source pixel, gutter clamped to the edge.
                    cov = bit(glyph, ay - PAD, ax - PAD);
                } else {
                    // Source-pixel coordinate of this atlas texel's centre.
                    float fx = ((ax - PAD) + 0.5f) / SS - 0.5f;
                    float fy = ((ay - PAD) + 0.5f) / SS - 0.5f;
                    int   x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
                    float tx = fx - x0, ty = fy - y0;
                    float c00 = bit(glyph, y0,     x0);
                    float c10 = bit(glyph, y0,     x0 + 1);
                    float c01 = bit(glyph, y0 + 1, x0);
                    float c11 = bit(glyph, y0 + 1, x0 + 1);
                    cov = (c00 * (1 - tx) + c10 * tx) * (1 - ty)
                        + (c01 * (1 - tx) + c11 * tx) * ty;
                    // smoothstep(0.5 - edge, 0.5 + edge, cov)
                    float t = (cov - (0.5f - edge)) / (2.0f * edge);
                    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                    cov = t * t * (3.0f - 2.0f * t);
                }
                Uint8 a = (Uint8)(cov * 255.0f + 0.5f);
                pixels[ay * atlas_w + (cell_x + ax)] = white_rgb | ((Uint32)a << Ashift);
            }
        }
    }
    SDL_UnlockSurface(surface);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surface);
    SDL_FreeSurface(surface);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

void VgaTextEngine::draw_char(unsigned char c, int x, int y, SDL_Color fg, SDL_Color bg, SDL_Texture* tex) {
    if (!tex) tex = font_texture;
    SDL_Rect d = { x * FONT_CHAR_WIDTH, y * FONT_CHAR_HEIGHT, FONT_CHAR_WIDTH, FONT_CHAR_HEIGHT };
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a); SDL_RenderFillRect(renderer, &d);
    if (c != ' ' && c != 0) {
        // Sample only the inner glyph; the padded gutter absorbs the filter's reach.
        // The glyph is atlas_ss_x native size in the atlas (1x unless SHARP); the GPU
        // scales that single source rect straight to the destination cell, so the
        // supersampling is resolved in one linear sample with no double-blur.
        const int SS     = atlas_ss_;
        const int glyph_w = FONT_CHAR_WIDTH  * SS;
        const int glyph_h = FONT_CHAR_HEIGHT * SS;
        const int cell_w = glyph_w + 2 * FONT_ATLAS_PAD;
        SDL_Rect s = { c * cell_w + FONT_ATLAS_PAD, FONT_ATLAS_PAD, glyph_w, glyph_h };
        SDL_SetTextureColorMod(tex, fg.r, fg.g, fg.b); SDL_RenderCopy(renderer, tex, &s, &d);
    }
}

void VgaTextEngine::draw_cursor_block(int x, int y) {
    int y_start = (y * FONT_CHAR_HEIGHT) + (int)(FONT_CHAR_HEIGHT * 0.85);
    int h_size = (int)(FONT_CHAR_HEIGHT * 0.15);
    if(h_size < 2) h_size = 2;
    SDL_Rect d = { x * FONT_CHAR_WIDTH, y_start, FONT_CHAR_WIDTH, h_size };
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255); SDL_RenderFillRect(renderer, &d);
}

void VgaTextEngine::pixelToGrid(int px, int py, int* gx, int* gy) {
    // 1. Account for Scaling
    int logical_x = (int)(px / scale_factor);
    int logical_y = (int)(py / scale_factor);

    // 2. Divide by Char Size
    int col = logical_x / FONT_CHAR_WIDTH;
    int row = logical_y / FONT_CHAR_HEIGHT;

    // 3. Convert to 1-based index (Borland style)
    *gx = col + 1;
    *gy = row + 1;
}
