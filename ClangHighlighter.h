#pragma once
#include "EditorBuffer.h"

class BuildSystem;

class ClangHighlighter {
public:
    // Spawns a background thread that tokenises buffer with libclang and fills
    // buffer.semantic_cache->colors.  Safe to call from the main thread at any
    // time; concurrent requests are coalesced via the version counter.
    static void requestHighlight(EditorBuffer& buffer, BuildSystem* build);
};
