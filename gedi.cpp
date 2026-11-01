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

    // Before probing: a shipped copy carries its own compiler and build systems,
    // and this puts them at the front of PATH. The probe below then finds them,
    // and every cmake/ninja/meson/make the editor spawns resolves to them too.
    DependencyChecker::useBundledTools();

    Toolchain toolchain;
    if (!DependencyChecker::check(toolchain, ignore_deps))
        return 1;

    // A toolchain the installer configured - MSVC, a system Clang, MinGW or
    // Open Watcom - comes with the directories and variables it needs to run.
    // Applied after the bundled set so it takes precedence: this is the compiler
    // actually in use, and its linker must be the one found first.
    DependencyChecker::applyToolchainEnv(toolchain);

    TextEditor editor;
    editor.run(argc, argv);
    return 0;
}
