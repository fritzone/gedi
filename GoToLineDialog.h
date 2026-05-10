#ifndef GOTOLINEDIALOG_H
#define GOTOLINEDIALOG_H

#include "Renderer.h"

class GoToLineDialog {
public:
    static int show(Renderer& renderer, int current_line, int max_lines);
};

#endif // GOTOLINEDIALOG_H
