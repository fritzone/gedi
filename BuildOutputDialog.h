#ifndef BUILDOUTPUTDIALOG_H
#define BUILDOUTPUTDIALOG_H

#include "Renderer.h"
#include <string>
#include <vector>

class BuildOutputDialog {
public:
    static void show(Renderer& renderer, const std::vector<std::string>& lines);
};

#endif // BUILDOUTPUTDIALOG_H
