#ifndef COMPILERSETTINGS_H
#define COMPILERSETTINGS_H

#include <map>
#include <string>

// The option values of one compiler's catalogue (see CompilerDefs.h): option
// id -> 0/1 for a checkbox, or the index of the chosen radio choice.
struct CompilerOptionValues {
    std::map<std::string, int> values;
    std::string optional_flags;         // typed by the user, passed as they are
};

// Build options of a file or a project. The C++ standard is shared by every
// compiler (libclang and CMake use it too); everything else is kept per option
// catalogue - Definition::settings_id, e.g. "gnu" for GCC, Clang and the GCC
// cross compilers, "watcom", "msvc" - so switching compilers loses nothing.
// An option not stored here has its definition's default.
struct CompilerSettings {
    std::string cpp_standard = "c++17";
    std::map<std::string, CompilerOptionValues> compilers;   // by settings id
};

#endif // COMPILERSETTINGS_H
