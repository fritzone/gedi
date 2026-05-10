#ifndef COMPILEOPTIONSDIALOG_H
#define COMPILEOPTIONSDIALOG_H

#include "Renderer.h"
#include "BuildSystem.h"
#include "EditorBuffer.h"

class CompileOptionsDialog {
public:
    static void show(Renderer& renderer, BuildSystem& buildSystem, EditorBuffer& buffer);
};

#endif // COMPILEOPTIONSDIALOG_H
