#ifndef HELPDIALOG_H
#define HELPDIALOG_H

#include "Renderer.h"
#include "HelpProvider.h"
#include <string>
#include <vector>

class HelpDialog {
public:
    static void show(Renderer& renderer, HelpProvider& helpProvider, std::vector<std::string>& help_history);
};

#endif // HELPDIALOG_H
