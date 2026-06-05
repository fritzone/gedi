#pragma once
#include "Constants.h"
#include <memory>

// Forward declarations to avoid circular includes
class BorlandEngine;
class WindowManager;

// Shared Global Variables (Defined in main.cpp)
// We use 'extern' here to tell the compiler these exist somewhere else
extern BorlandEngine eng;
extern WindowManager wm;

// UI State
extern int currentMenuIndex; 
extern int currentItemIndex;  
extern int openW; 
extern bool isResizing;