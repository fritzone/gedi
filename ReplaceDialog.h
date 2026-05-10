#ifndef REPLACEDIALOG_H
#define REPLACEDIALOG_H

#include "Renderer.h"
#include <string>

enum class ReplaceAction {
    CANCEL,
    REPLACE,
    REPLACE_ALL
};

struct ReplaceResult {
    ReplaceAction action;
    std::string find_term;
    std::string replace_term;
};

class ReplaceDialog {
public:
    static ReplaceResult show(Renderer& renderer, const std::string& initial_find, const std::string& initial_replace);
};

#endif // REPLACEDIALOG_H
