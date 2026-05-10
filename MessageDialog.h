#ifndef MESSAGEDIALOG_H
#define MESSAGEDIALOG_H

#include "Renderer.h"
#include <string>

class MessageDialog {
public:
    static void show(Renderer& renderer, const std::string& message);
};

#endif // MESSAGEDIALOG_H
