# gedi install builder

A retro, Turbo-era setup system for the Windows build. It produces one
self-contained `gedi-setup.exe` that carries the editor, its DLLs, the fonts,
the images, the configuration and the build templates, and installs them behind
a full-screen DOS-style wizard.

The wizard is not a lookalike: it draws on the same `VgaTextEngine` character
grid gedi itself runs on, through the same `curses_compat` layer and the same
`Renderer`. The shaded desktop, the double-ruled panel, the drop shadows and the
buttons are gedi's own, so setup and the editor are visibly one program. Buttons
push in when activated - by mouse, Enter or hotkey - with the same 120/80 ms
timing `DialogBase::runPressAnimation` uses in the editor.

Setup takes the whole screen: borderless, with the 80x25 character cell scaled
up to fill the display, the way a DOS install program simply owned the monitor.
It keeps normal window z-order though, so you can still Alt-Tab to another
application while it is running.

```
installer/
  install.yaml          the install script - what gets packed and how it behaves
  common/Yaml.*         a small YAML reader (-> nlohmann::json)
  common/Archive.*      container format, LZMS compression, CRC
  builder/main.cpp      gedi-mkinstall: script + files + stub -> installer exe
  stub/main.cpp         setup entry: bootstrap, then the wizard
  stub/Wizard.*         the Turbo-style pages
  stub/Payload.*        reads the payload welded onto this executable
  stub/Install.*        files, shortcuts, registry, uninstaller
```

## Building

The `gedi-setup` target does everything:

```
nmake gedi-setup            # after a normal build
```

It runs `gedi-mkinstall` over `install.yaml` with `--root` pointing at
gedi-gui's output directory - the editor's existing POST_BUILD steps have
already gathered every payload file there - and drops `gedi-setup.exe` next to
the editor. To drive it by hand:

```
gedi-mkinstall installer/install.yaml --root build --stub build/installer/setup-stub.exe -o build/gedi-setup.exe
```

## How one executable holds everything

A finished installer is the stub's PE image with the payload appended:

```
[ setup-stub.exe ][ file 0 ][ file 1 ]...[ TOC (json) ][ Trailer ]
```

Appending to a PE image leaves it perfectly runnable, so the stub finds its own
payload by opening its own module and reading the fixed-size trailer from the
end. The TOC is json, so it can carry the entire install script - ui text,
components, shortcuts - alongside each file's extent.

Compression is LZMS through the Windows Compression API (`Cabinet.dll`), which
beats vendoring a compression library: no third-party code, and ratios in LZMA
territory. On the gedi payload it turns 148 MB into 34 MB, most of that being
libclang.dll going from 141 MB to 32 MB. Each entry records its own method, so
a builder on a platform without the API just stores bytes and the stub still
installs it.

### The bootstrap trick

The wizard needs SDL2 and the VGA fonts before it can draw anything, but those
live *inside* the payload. So SDL2.dll cannot be an ordinary load-time import -
the process would fail to start.

The stub therefore **delay-loads** it (`/DELAYLOAD:SDL2.dll`). At startup it
unpacks just the files listed under `bootstrap:` into a scratch directory,
points `SetDllDirectory` and the working directory at it, and only then touches
anything that calls SDL. The font search in `curses_compat` tries a plain
relative name first, so `VGA9.F16` is found there with no change to gedi.

The stub is built without `GEDI_HAVE_SDL_IMAGE` on purpose: keeping SDL2_image
out means the bootstrap is three small files rather than a PNG decoder and its
two support libraries.

### Going full screen

`gui_set_fullscreen(1)` in `curses_compat` switches the SDL window to
`FULLSCREEN_DESKTOP` and sets `scale_factor` to `min(w / (80*8), h / (25*16))`
so the character grid fills the display. Whatever does not divide evenly becomes
a few extra rows or columns, which simply extends the shaded desktop; the panel
stays centred. Anything holding cached dimensions must refresh afterwards - the
hook pushes a `KEY_RESIZE` for that, and the stub calls
`Renderer::updateDimensions()`.

Two things it deliberately does **not** do:

* **always-on-top.** A topmost full-screen window cannot be switched away from,
  which makes the machine feel hijacked while setup is open. Normal z-order lets
  Alt-Tab bring another application in front.
* **minimise on focus loss.** SDL does that by default for full-screen windows,
  which would mean restoring setup from the taskbar instead of Alt-Tabbing back,
  so the hook clears `SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS`.

### The uninstaller

The first `payload_base` bytes of the installer are the bare stub, so install
time writes exactly that prefix out as `uninstall.exe` - a working uninstaller
with none of the payload's weight. It finds `uninstall.dat` (a manifest of every
file, shortcut and registry key written) beside itself, and runs the same UI in
removal mode. SDL2 and the fonts are already in the install directory, so it
needs no bootstrap of its own.

## The bundled C++ toolchain

The installer carries a complete Clang toolchain so a fresh Windows machine can
build C++ with no Visual Studio, no MinGW and nothing on PATH. It is staged into
`thirdparty\toolchain` by

```
.\thirdparty\fetch-toolchain.ps1
```

and packed as the `toolchain` component, landing at `<install dir>\toolchain`.
Like `sdl2\` and `thirdparty\llvm\`, the binaries are **not committed** - run the
script after a clean checkout.

It is [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) rather than the
official LLVM Windows build, because the latter targets the MSVC ABI and needs
Visual Studio's headers and libraries present - not self-contained. llvm-mingw
ships clang, lld, the mingw-w64 CRT headers and import libraries, libc++ and the
runtime DLLs in one redistributable tree (Apache-2.0 with LLVM exception).

The release is trimmed from 715 MB to about 404 MB: the aarch64, armv7 and i686
sysroots go, along with clangd, clang-tidy and LLDB (see `-KeepLldb`).

**Static linking is the default.** `bin\mingw32-common.cfg` has `-static`
appended, because otherwise every program built here needs `libc++.dll`,
`libunwind.dll` and `libwinpthread-1.dll` beside it, and a freshly built program
dies at startup with no diagnostic at all. `-Wno-unused-command-line-argument`
rides along to silence the once-per-file warning `-static` provokes when only
compiling.

### How the editor finds it

`DependencyChecker` prefers the bundled toolchain over anything on the machine,
resolving it relative to the running executable - `<exe dir>\toolchain` when
installed, and the sibling `thirdparty\toolchain` when running from `build\`.
`BuildSystem` then passes `-DCMAKE_CXX_COMPILER` / `-DCMAKE_C_COMPILER` to cmake
and `CXX=` to make, so project builds use the same compiler as single-file
compiles instead of whatever cmake's own detection turns up.

The probe result is cached in `toolchain.json`, which is trusted on later runs.
A machine that already recorded a compiler before installing keeps using it;
delete that file to re-probe. A recorded compiler that no longer exists on disk
now triggers a re-probe by itself.

## The bundled build systems

`thirdparty\buildtools` carries the three build systems the editor drives, so a
generated project builds on a machine with nothing installed:

| tool  | version | source                          |
|-------|---------|---------------------------------|
| CMake | 4.4.3   | Kitware release zip (BSD-3)     |
| Ninja | 1.13.2  | ninja-win.zip (Apache-2.0)      |
| Meson | 1.12.1  | source tarball (Apache-2.0)     |

About 113 MB. CMake's `doc\` HTML manual is dropped; the editor never opens it.

Meson has no official standalone Windows binary - it is pure Python - so it is
assembled rather than unpacked: the embeddable CPython 3.13 distribution goes in
`meson\python`, meson's own `mesonbuild\` beside it, and a `meson.cmd` shim ties
them together. That distribution takes its whole `sys.path` from `python3xx._pth`
and does **not** add the script's directory, so the staging step appends `..` to
that file or `import mesonbuild` fails.

GNU make comes from the compiler side, where llvm-mingw ships it only as
`mingw32-make.exe`; the staging step also copies it to `make.exe`, which is what
the editor and most Makefiles actually invoke.

### Putting them on PATH

`DependencyChecker::useBundledTools()`, called from `main()` before anything
else, prepends `toolchain\bin` and the three `buildtools` directories to `PATH`
- writing both `SetEnvironmentVariable` (what child processes inherit) and
`_putenv_s` (what the CRT's own `getenv`/`_popen` read; setting one leaves the
other stale). After that, plain `cmake`, `ninja`, `meson` and `make` resolve to
the shipped copies for every command the editor spawns.

Meson is additionally handed `CXX` at setup time and cmake `-DCMAKE_CXX_COMPILER`,
so neither runs its own detection and lands on a different compiler than the rest.

## The debugger

LLDB is staged with the compiler and is what the debugger pane drives on
Windows. `-NoLldb` leaves it out for a compile-only build; it costs `liblldb`
plus a CPython, about 68 MB.

Nothing speaks LLDB's own protocol: the bridge is **`lldb-mi`**, a GDB/MI front
end on top of LLDB. It identifies itself as `GNU gdb (GDB) 7.4 / This is a MI
stub on top of LLDB and not GDB`, which is exactly the point - the editor's
existing MI backend drives it unchanged.

Two things had to happen for that to work:

* **`GdbDebugger` was Linux-only.** Every method had a `#ifndef _WIN32` body and
  an empty Windows stub, and the factory handed Windows a `MsvcDebugger` that
  does nothing - so the debugger pane had no backend at all there. The protocol
  layer was already platform-neutral, so the pipe handling moved behind three
  helpers (`ioWrite`, `ioRead`, `ioShutdown`) with a `CreatePipe`/`CreateProcess`
  implementation beside the POSIX one, and everything above it is now shared.
* **The backend is no longer GDB-specific.** `findDebugger()` walks PATH for
  `gdb` first and falls back to `lldb-mi`, `name()` reports whichever was found,
  and the launch differs only in the command line - lldb-mi takes the program as
  a bare argument, gdb needs `--interpreter=mi2 -q --nx`. `pause()` sends SIGINT
  on POSIX and the protocol's own `-exec-interrupt` on Windows, where there is
  no signal to send.

Because the toolchain directory is on PATH (see above), `lldb-mi` is found
automatically by an installed copy.

Expect rough edges: lldb-mi is a partial MI implementation, and it is chattier
than gdb - it emits a `=library-loaded` record for every DLL the target touches.

## Icons

Windows takes an executable's icon from an embedded resource, not from a PNG on
disk, so `icons\make-ico.ps1` turns the artwork into multi-resolution `.ico`
containers (16-256, DIB entries below 256 and a PNG entry at 256, aspect
preserved rather than stretched) and two `.rc` files embed them:

| resource        | icon                   | covers                                          |
|-----------------|------------------------|-------------------------------------------------|
| `gedi.rc`       | `icons\gt_gui.ico`     | the editor's file icon, taskbar, shortcuts      |
| `installer\setup.rc` | `icons\gedi_setup.ico` | the installer and the uninstaller           |

Both use resource **id 1** deliberately: Explorer shows the lowest-numbered icon
as the file's icon, and SDL takes the window icon - what the taskbar and Alt-Tab
show - from the executable's first icon too, so one entry covers all of it with
no runtime `SDL_SetWindowIcon` call.

The shortcuts inherit it for free: `install.yaml` points their `icon:` at the
executable, and `SetIconLocation(exe, 0)` resolves to the embedded resource. The
uninstaller likewise needs no icon of its own - it is the installer's own PE
prefix written back out, so it carries the same resource.

## The install script

Paths under `from:` and `dir:` resolve against `--root`.

```yaml
product:
  name: gedi
  version: '1.0.0'
  publisher: 'Ferenc Deak'
  website: 'https://example.org'
  install_key: gedi              # Add/Remove Programs registry key name

ui:
  title: 'gedi Installation Utility'
  welcome: |                     # shown on page 1
  license: |                     # scrollable; the page is skipped if absent
  finish:  |                     # shown when done

install:
  default_dir: '%LOCALAPPDATA%\Programs\gedi'   # %VARS% are expanded
  register_uninstall: true
  start_menu: true
  desktop_shortcut: false

bootstrap:                       # unpacked before the UI starts
  - SDL2.dll
  - VGA9.F16

components:
  - id: core
    name: 'Program files'
    description: 'Shown under the component list.'
    required: true               # cannot be unticked
    files:
      - from: gedi-gui.exe       # to: defaults to the file name
      - from: icons/logo.png
        to: icons/logo.png
  - id: extrafonts
    name: 'Additional fonts'
    selected: true               # ticked by default, user may untick
    files:
      - dir: fonts               # a whole directory
        to: fonts
        pattern: '*.F16'         # optional, default *
        recurse: true            # optional, default true

shortcuts:
  - name: gedi
    target: gedi-gui.exe         # relative to the install directory
    location: start_menu         # start_menu | desktop
    group: gedi                  # start-menu folder
    description: 'gedi editor'
    icon: gedi-gui.exe
```

Supported YAML is a practical subset - block mappings and sequences, flow
sequences, `|` and `>` block scalars, quoted and plain scalars, comments. It is
deliberately small, in the same spirit as `TemplateEngine`.

## What it writes

Nothing outside the chosen directory, except:

* the shortcuts the script asks for, under the **per-user** Start Menu / Desktop;
* one key under `HKCU\...\CurrentVersion\Uninstall\<install_key>`.

The default install directory is under `%LOCALAPPDATA%`, so no elevation is
needed. Pointing it at `%ProgramFiles%` will fail without admin rights - the
directory page warns when the target does not look writable.
