#ifndef COMPILERSETTINGS_H
#define COMPILERSETTINGS_H

#include <string>
#include <vector>

struct CompilerSettings {
    std::string cpp_standard = "c++17";
    
    // Tab 1: Basic
    bool debug_symbols = true;
    int optimization_level = 0; // 0: -O0, 1: -O2, 2: -O3
    bool wall = true;
    bool wextra = true;
    bool wpedantic = false;
    bool werror = false;

    // Tab 2: Advanced
    bool wconversion = false;
    bool wsign_conversion = false;
    bool wshadow = false;
    bool wnon_virtual_dtor = false;
    bool wold_style_cast = false;
    bool woverloaded_virtual = false;
    bool wnull_dereference = false;
    bool wdouble_promotion = false;
    bool wformat_2 = false;
    
    bool fno_omit_frame_pointer = false;
    bool fsanitize_address_ub = false;
    bool fsanitize_leak = false;
    bool flto = false;
    
    bool march_native = false;
    bool mtune_native = false;

    // Tab 3: Expert
    bool wcast_align = false;
    bool wcast_qual = false;
    bool wswitch_enum = false;
    bool wundef = false;
    bool wredundant_decls = false;
    bool wlogical_op = false;
    bool wuseless_cast = false;
    bool weffcxx = false;
    
    bool fno_exceptions = false;
    bool fno_rtti = false;
    bool fvisibility_hidden = false;
    bool fstrict_aliasing = false;
    
    bool fsanitize_pointer_compare = false;
    bool fsanitize_pointer_subtract = false;
    
    bool wl_as_needed = false;
    bool wl_o1 = false;

    std::string optional_flags;
};

#endif // COMPILERSETTINGS_H
