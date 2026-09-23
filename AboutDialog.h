#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include "Renderer.h"
#include <string>

// The product "About" box. A proper framed dialog (product name, version,
// description, copyright) with an OK button. In the graphical (SDL) build it also
// shows the product logo passed as image_path; the text-mode build ignores it.
class AboutDialog {
public:
    static void show(Renderer& renderer, const std::string& image_path = "");
};

#endif // ABOUTDIALOG_H
