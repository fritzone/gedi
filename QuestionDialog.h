#ifndef QUESTIONDIALOG_H
#define QUESTIONDIALOG_H

#include "Renderer.h"
#include <string>

class QuestionDialog {
public:
    static int ask(Renderer& renderer, const std::string& question, const std::string& info = "");
};

#endif // QUESTIONDIALOG_H
