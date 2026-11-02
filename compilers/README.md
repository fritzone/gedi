# Compiler definitions

Each `*.json` file here describes one C++ compiler to gedi: how to recognise it,
how to call it, what its error messages look like, what it can do, and the
options **Build > Compile Options** offers for it.

gedi reads the shipped definitions (this directory, installed next to the
executable or in `share/gedi/compilers`) and then your own, from
`~/.config/gedi/compilers/` (`%USERPROFILE%\.config\gedi\compilers` on Windows).
A file of yours with the same `id` as a shipped one replaces it.

**Options > Compiler Options > New...** writes a definition for a compiler gedi
does not know, based on one of the shipped kinds; edit that file to refine it.
**Build > Compile Options > Test** compiles a one-line program once per option
and marks the options the compiler rejects.

## Inheritance

`"inherits": "<id>"` starts from another definition and changes only what is
given: objects are merged key by key, anything else (strings, numbers, arrays)
replaces. `"abstract": true` marks a base that is not offered as a compiler
itself (not inherited). `"remove_options": ["id", ...]` drops inherited options
(and is itself inherited, so a base can drop host-only options for all its
heirs). `"extra_options": [tabs]` adds tabs to an inherited catalogue instead of
replacing it the way `options` does.

Option values are stored per option catalogue: a definition that declares
`options` gets its own; one that inherits them - adding a `top_option` or
`extra_options` or not - shares its parent's stored values (GCC, Clang, the
cross compilers and the vendor builds all share `gnu`'s), unless it says
`"settings": "<own id>"`.

## Fields

```jsonc
{
  "id": "arm-gcc",                  // unique; defaults to the file name
  "name": "ARM GCC",                // shown in the compiler list
  "description": "...",             // shown under it
  "inherits": "gcc-cross",
  "order": 100,                     // position in the list
  "platforms": ["linux"],           // only offered there: linux, windows, macos

  "setup": "gcc",                   // environment: gcc, clang, msvc, watcom or plain
                                    //   (plain = the executable's directory on PATH)
  "match": {                        // recognising an executable
    "executables": ["arm-none-eabi-g++"],   // file names, '*' wildcard, no .exe
    "version_args": "--version",
    "version_regex": "arm-none-eabi",       // must match that output, if given
    "target": "cross",                      // host / cross, by -dumpmachine (GCC-style only)
    "priority": 5                           // tried before others the name also fits
  },

  "command": {
    "args": [],                             // right after the executable: ["c++"] for "zig c++"
    "always": ["--specs=nosys.specs"],      // every compile gets these
    "output": "-o {out}",                   // naming the executable
    "compile_only": "-c",                   // compiling without linking (Test uses it
    "object_output": "-o {out}",            //   when linking fails)
    "include": "-I{dir}", "define": "-D{def}",
    "exe_ext": "host",                      // "host" (.exe on Windows only) or e.g. ".elf"
    "host_libraries": false,                // keep the host's -l/-L and /usr includes
    "scratch_dir": false,                   // compile from an empty, removed directory
    "keep_outputs": [".map"],               // moved out of that directory
    "standard_flag": "-std={std}",          // empty: the compiler has no standard switch
    "standards": ["c++11", "c++14", "c++17"],
    "standard_flags": { "c++11": "" }       // per-standard overrides
  },

  "diagnostics": "gnu",             // gnu, msvc, watcom - or a pattern of its own:
  // "diagnostics": { "regex": "^(.+?):(\\d+): (error|warning): (.*)",
  //                  "file": 1, "line": 2, "column": 0, "severity": 3, "message": 4 },

  "capabilities": {
    "runs_on_host": false,          // Run is offered only when true
    "debuggable": false,            // the debugger is offered only when true
    "parse_headers": "self",        // highlighting asks this compiler (self) or the host's c++
    "make": true, "meson": true,
    "cmake": { "supported": true, "generator": "", "compiler": "",   // a sibling for CMake
               "build_dir": "build", "optional_flags": true }
  },

  "top_option": { ... },            // one radio option for the top row; with a standard
                                    //   switch it leads the first tab instead

  "options": [                      // the Compile Options tabs
    { "tab": "Basic", "groups": [
      { "name": "Optimization", "items": [
        // a checkbox
        { "id": "debug_symbols", "label": "Generate debug symbols", "flag": "-g",
          "default": true, "role": "debug" },
        // a radio group: one of its choices is always in effect
        { "id": "optimization", "role": "optimization", "default": 0, "choices": [
          { "label": "Disable optimisation", "flag": "-O0" },
          { "label": "Optimise for size",    "flag": "-Os" } ] }
      ] }
    ] }
  ]
}
```

An option or choice may give `"cmake_flag"`: what goes into `CMAKE_CXX_FLAGS`
instead of `flag` (`""` for nothing). `role` marks the options that pick the
CMake build type: `debug` (a checkbox) and `optimization` (a radio group whose
first choice means none).

## Example: a bare-metal ARM compiler

```json
{
  "id": "arm-gcc",
  "name": "ARM GCC (bare metal)",
  "inherits": "gcc-cross",
  "match": { "executables": ["arm-none-eabi-g++"] },
  "command": { "always": ["--specs=nosys.specs"], "exe_ext": ".elf" }
}
```

## Shipped definitions

Abstract bases (`gnu`, `generic`) are not listed. *Platforms* is where the entry is offered.

### Native compilers

| Compiler | id | Executables | Platforms |
|---|---|---|---|
| GCC | `gcc` | g++, g++-*, c++, *-g++, *-g++-* | all |
| Clang / LLVM | `clang` | clang++, clang++-*, c++ | all |
| Apple Clang (Xcode) | `apple-clang` | clang++, c++ | macos |
| AMD Optimizing C/C++ (AOCC) | `aocc` | clang++ | linux |
| Intel oneAPI DPC++/C++ (icpx) | `intel-icpx` | icpx | linux |
| Intel C++ Classic (icpc) | `intel-icpc` | icpc | linux |
| IBM Open XL C/C++ | `ibm-openxl` | ibm-clang++, ibm-clang++_r, xlclang++ | all |
| HPE Cray C++ (CCE) | `cray-cce` | CC, crayCC | linux |
| Circle | `circle` | circle | linux |
| Cosmopolitan (cosmoc++) | `cosmocc` | cosmoc++, x86_64-unknown-cosmo-c++ | all |
| Zig (zig c++) | `zig` | zig | all |
| GCC cross compiler | `gcc-cross` | (inherited) | all |
| Clang cross compiler | `clang-cross` | clang++, clang++-*, *-clang++, *-clang++-* | all |

### Windows

| Compiler | id | Executables | Platforms |
|---|---|---|---|
| Microsoft Visual C++ (MSVC) | `msvc` | cl | windows |
| Clang-cl (MSVC-compatible Clang) | `clang-cl` | clang-cl | windows |
| Intel oneAPI C++ for Windows (icx) | `intel-icx` | icx, icx-cl | windows |
| Borland / Embarcadero C++ (bcc32) | `bcc32` | bcc32 | windows |
| Embarcadero C++ (bcc32c / bcc64) | `bcc-clang` | bcc32c, bcc32x, bcc64, bcc64x | windows |
| Digital Mars C++ | `dmc` | dmc | windows |
| Open Watcom | `watcom` | owcc, wcl386, wpp386 | all |

### GPU and HPC

| Compiler | id | Executables | Platforms |
|---|---|---|---|
| NVIDIA CUDA (nvcc) | `nvcc` | nvcc | all |
| NVIDIA HPC SDK (nvc++) | `nvhpc` | nvc++, pgc++ | linux |
| AMD ROCm HIP (hipcc) | `hipcc` | hipcc | linux, windows |

### Unix vendor compilers

| Compiler | id | Executables | Platforms |
|---|---|---|---|
| Oracle Developer Studio C++ (CC) | `oracle-cc` | CC | all |
| HP aC++ (HP-UX) | `hp-acc` | aCC | all |
| IBM XL C/C++ (xlC) | `ibm-xlc` | xlC, xlC_r, xlc++, xlc++_r | all |

### Embedded and bare metal

| Compiler | id | Executables | Platforms |
|---|---|---|---|
| Arm GNU Toolchain (arm-none-eabi) | `arm-gcc` | arm-none-eabi-g++ | all |
| Arm Compiler 6 (armclang, Keil MDK) | `armclang` | armclang | all |
| Arm Compiler 5 (armcc, legacy Keil) | `armcc` | armcc | all |
| IAR C/C++ for Arm (iccarm) | `iar-arm` | iccarm | all |
| TI Arm Clang (tiarmclang) | `tiarmclang` | tiarmclang | all |
| RISC-V GNU Toolchain (bare metal) | `riscv-gcc` | riscv64-unknown-elf-g++, riscv32-unknown-elf-g++, riscv-none-elf-g++, riscv64-elf-g++, riscv32-elf-g++, riscv*-elf-g++ | all |
| AVR GCC (avr-g++, Arduino) | `avr-gcc` | avr-g++ | all |
| MSP430 GCC (msp430-elf-g++) | `msp430-gcc` | msp430-elf-g++ | all |
| ESP32 Xtensa GCC (ESP-IDF) | `esp32-gcc` | xtensa-esp32-elf-g++, xtensa-esp32s2-elf-g++, xtensa-esp32s3-elf-g++, xtensa-esp-elf-g++ | all |
| Microchip XC32 (PIC32) | `xc32` | xc32-g++ | all |
| AArch64 bare-metal GCC | `aarch64-elf-gcc` | aarch64-none-elf-g++, aarch64-elf-g++ | all |

### Cross to other systems and the web

| Compiler | id | Executables | Platforms |
|---|---|---|---|
| MinGW-w64 cross compiler (to Windows) | `mingw-cross` | x86_64-w64-mingw32-g++, i686-w64-mingw32-g++, *-w64-mingw32-g++* | linux, macos |
| Android NDK Clang | `android-ndk` | *-linux-android*-clang++, *-linux-androideabi*-clang++ | all |
| WASI SDK (WebAssembly) | `wasi-sdk` | clang++, *-wasi-clang++, wasm32-wasi-clang++ | all |
| Emscripten (em++) | `emscripten` | em++ | all |

### Retro and homebrew

| Compiler | id | Executables | Platforms |
|---|---|---|---|
| DJGPP (32-bit DOS) | `djgpp` | i586-pc-msdosdjgpp-g++, i386-pc-msdosdjgpp-g++, i686-pc-msdosdjgpp-g++ | all |
| GCC IA-16 (16-bit DOS) | `gcc-ia16` | ia16-elf-g++ | all |
| Amiga GCC (m68k-amigaos) | `amiga-gcc` | m68k-amigaos-g++ | all |
| Atari ST GCC (m68k-atari-mint) | `atari-gcc` | m68k-atari-mint-g++, m68k-atari-mintelf-g++ | all |
| devkitARM (GBA / DS / 3DS) | `devkitarm` | arm-none-eabi-g++ | all |
| devkitPPC (GameCube / Wii) | `devkitppc` | powerpc-eabi-g++ | all |
| devkitA64 (Nintendo Switch) | `devkita64` | aarch64-none-elf-g++ | all |
| PSP SDK (psp-g++) | `psp-gcc` | psp-g++ | all |
| PS2SDK (PlayStation 2 EE) | `ps2-gcc` | mips64r5900el-ps2-elf-g++, ee-g++ | all |
| PSn00bSDK (PlayStation 1) | `psx-gcc` | mipsel-none-elf-g++ | all |
| libdragon (Nintendo 64) | `n64-gcc` | mips64-elf-g++ | all |
| KallistiOS (Sega Dreamcast) | `dreamcast-gcc` | sh-elf-g++, kos-c++ | all |
