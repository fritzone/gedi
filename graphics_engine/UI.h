#pragma once
#include <SDL2/SDL.h>

void RenderUI();
void HandleUIInput(SDL_Keycode key);
bool HandleMouseUI(int gx, int gy); // Click handler
void HandleMenuHover(int gx, int gy); // NEW: Hover handler
void newFileWindow();
