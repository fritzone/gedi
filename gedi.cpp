#include "TextEditor.h"
#include "DependencyChecker.h"

#include <string>

int main(int argc, char* argv[]) {
    bool ignore_deps = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--ignore-dependencies") {
            ignore_deps = true;
            break;
        }
    }

    Toolchain toolchain;
    if (!DependencyChecker::check(toolchain, ignore_deps))
        return 1;

    TextEditor editor;
    editor.run(argc, argv);
    return 0;
}
