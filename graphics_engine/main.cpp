#include <SDL2/SDL.h>
#include "Constants.h"
#include "GlobalState.h"
#include "BorlandEngine.h"
#include "WindowSystem.h"
#include "UI.h"

// Define Global State
BorlandEngine eng;
WindowManager wm;
int currentMenuIndex = -1;
int currentItemIndex = 0;
int openW = 0;
bool isResizing = false;

void HandleInput(SDL_Event e) {
    if (e.type == SDL_WINDOWEVENT) {
        if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
            eng.handleResize(e.window.data1, e.window.data2);
            wm.notifyResize(eng.SCREEN_COLS, eng.SCREEN_ROWS);
        }
        else if (e.window.event == SDL_WINDOWEVENT_LEAVE) {
            SDL_ShowCursor(SDL_ENABLE);
            eng.updateMousePos(-1, -1, false);
        }
        else if (e.window.event == SDL_WINDOWEVENT_ENTER) {
            SDL_ShowCursor(SDL_DISABLE);
        }
        return;
    }

    if (e.type == SDL_MOUSEMOTION) {
        int gx, gy;
        eng.pixelToGrid(e.motion.x, e.motion.y, &gx, &gy);
        eng.updateMousePos(gx, gy, true);
        HandleMenuHover(gx, gy);

        // Handle Dragging if active
        wm.handleMouseDrag(gx, gy);
    }

    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        wm.handleMouseUp();
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        int gx, gy;
        eng.pixelToGrid(e.button.x, e.button.y, &gx, &gy);

        if (HandleMouseUI(gx, gy)) return;
        if (wm.handleMousePress(gx, gy)) return;
    }

    if (e.type == SDL_KEYDOWN) {
        SDL_Keycode key = e.key.keysym.sym;
        Uint16 mod = e.key.keysym.mod;

        if ((mod & KMOD_CTRL) && (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS)) {
            eng.zoomIn(); wm.notifyResize(eng.SCREEN_COLS, eng.SCREEN_ROWS); return;
        }
        if ((mod & KMOD_CTRL) && (key == SDLK_MINUS || key == SDLK_KP_MINUS)) {
            eng.zoomOut(); wm.notifyResize(eng.SCREEN_COLS, eng.SCREEN_ROWS); return;
        }

        // NEW: Route to Question Dialog
        if (wm.questionDialog->active) {
            wm.questionDialog->handleInput(key, mod);
            return;
        }

        if (wm.fileDialog->active) {
            wm.fileDialog->handleInput(key, mod);
            return;
        }

        if (isResizing) { wm.handleResizeInput(key, mod); return; }

        if (key == SDLK_F6) { wm.nextWindow(); return; }
        if (key == SDLK_F5) {
            auto win = wm.getActiveWindow();
            if (win) {
                if (mod & KMOD_CTRL) wm.startResize();
                else if (mod & KMOD_ALT) win->toggleZoom();
                else win->toggleZoom();
            }
            return;
        }
        if (key == SDLK_F2) { wm.requestSave(false); return; }
        if (key == SDLK_F3) { wm.requestOpen(); return; }

        if (mod & KMOD_ALT) {
            if (key == SDLK_SPACE) { currentMenuIndex = M_SYSTEM; currentItemIndex = 0; return; }
            if (key == SDLK_f) { currentMenuIndex = M_FILE; currentItemIndex = 0; return; }
            if (key == SDLK_e) { currentMenuIndex = M_EDIT; currentItemIndex = 0; return; }
            if (key == SDLK_s) { currentMenuIndex = M_SEARCH; currentItemIndex = 0; return; }
            if (key == SDLK_r) { currentMenuIndex = M_RUN; currentItemIndex = 0; return; }
            if (key == SDLK_c) { currentMenuIndex = M_COMPILE; currentItemIndex = 0; return; }
            if (key == SDLK_d) { currentMenuIndex = M_DEBUG; currentItemIndex = 0; return; }
            if (key == SDLK_p) { currentMenuIndex = M_PROJECT; currentItemIndex = 0; return; }
            if (key == SDLK_o) { currentMenuIndex = M_OPTIONS; currentItemIndex = 0; return; }
            if (key == SDLK_w) { currentMenuIndex = M_WINDOW; currentItemIndex = 0; return; }
            if (key == SDLK_h) { currentMenuIndex = M_HELP; currentItemIndex = 0; return; }
            if (key == SDLK_x) { exit(0); }
            if (key == SDLK_F3) { wm.closeActiveWindow(); return; }
        }

        if (key == SDLK_F10) {
            if (currentMenuIndex == -1) { currentMenuIndex = M_FILE; currentItemIndex = 0; }
            else { currentMenuIndex = -1; }
            return;
        }

        if (currentMenuIndex != -1) {
            HandleUIInput(key);
        } else {
            auto activeWin = wm.getActiveWindow();
            if (activeWin) activeWin->handleInput(key, mod);
        }
    } else if (e.type == SDL_TEXTINPUT) {
        if (wm.fileDialog->active) { wm.fileDialog->handleTextInput(e.text.text); return; }
        bool altPressed = (SDL_GetModState() & KMOD_ALT);
        bool altGrPressed = (SDL_GetModState() & KMOD_RALT);
        if (currentMenuIndex == -1 && !isResizing && (!altPressed || altGrPressed)) {
            auto activeWin = wm.getActiveWindow();
            if (activeWin) activeWin->handleTextInput(e.text.text);
        }
    }
}

int main(int argc, char** argv) {
    eng.init_sdl();
    SDL_StartTextInput();
    bool quit = false;
    SDL_Event e;
    while(!quit) {
        while(SDL_PollEvent(&e)) {
            if(e.type == SDL_QUIT) quit = true;
            HandleInput(e);
        }
        RenderUI();
        eng.render_frame();
        SDL_Delay(16);
    }
    SDL_StopTextInput();
    eng.shutdown();
    return 0;
}
