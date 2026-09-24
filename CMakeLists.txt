cmake_minimum_required(VERSION 3.10)
project(gedi CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The sources are UTF-8 without a BOM and embed literal Unicode characters in
# wide-string literals (box-drawing glyphs, etc.). GCC/Clang assume UTF-8 source
# by default; MSVC assumes the active ANSI code page unless told otherwise, which
# silently corrupts those literals. /utf-8 sets both the source and execution
# character sets to UTF-8, matching Linux behaviour.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------------------
#  Dependencies common to both flavours
# ---------------------------------------------------------------------------
# libclang (semantic highlighting / go-to-definition)
#
# On Windows there is no system-wide libclang install, so we vendor a
# prebuilt one (DLL + hand-generated import lib + headers) under thirdparty/llvm.
if(WIN32)
    set(GEDI_LLVM_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/llvm" CACHE PATH "Root of the prebuilt libclang used on Windows")
endif()
find_path(CLANG_INCLUDE_DIR clang-c/Index.h HINTS ${GEDI_LLVM_ROOT}/include)
find_library(CLANG_LIBRARY NAMES clang libclang HINTS ${GEDI_LLVM_ROOT}/lib)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Clang DEFAULT_MSG CLANG_LIBRARY CLANG_INCLUDE_DIR)
if(NOT CLANG_FOUND)
    message(FATAL_ERROR "libclang not found. Please install libclang-dev (Linux) or populate thirdparty/llvm (Windows).")
endif()
if(WIN32)
    find_file(CLANG_DLL NAMES libclang.dll HINTS ${GEDI_LLVM_ROOT}/bin)
endif()

# On Windows, gedi-gui vendors its own SDL2 (no system package available).
if(WIN32)
    set(GEDI_SDL2_ROOT "${CMAKE_SOURCE_DIR}/sdl2" CACHE PATH "Root of the prebuilt SDL2 used on Windows")
    if(EXISTS "${GEDI_SDL2_ROOT}/include/SDL2/SDL.h")
        set(SDL2_FOUND TRUE)
        set(SDL2_INCLUDE_DIRS "${GEDI_SDL2_ROOT}/include")
        find_library(SDL2_LIBRARY NAMES SDL2 HINTS "${GEDI_SDL2_ROOT}/lib")
        set(SDL2_LIBRARIES ${SDL2_LIBRARY})
        set(SDL2_RUNTIME_DLLS
            "${GEDI_SDL2_ROOT}/lib/SDL2.dll"
        )
    endif()
endif()

# ---------------------------------------------------------------------------
#  Shared sources (backend independent)
# ---------------------------------------------------------------------------
set(GEDI_COMMON_SOURCES
    gedi.cpp
    EditorBuffer.cpp
    Renderer.cpp
    TextEditor.cpp
    SyntaxHighlighter.cpp
    ClangHighlighter.cpp
    FileBrowser.cpp
    ConfigManager.cpp
    BuildSystem.cpp
    SearchEngine.cpp
    HelpProvider.cpp
    BufferManager.cpp
    CompletionEngine.cpp
    KeyBindings.cpp
    MessageDialog.cpp
    AboutDialog.cpp
    QuestionDialog.cpp
    SettingsDialog.cpp
    ReplaceDialog.cpp
    GoToLineDialog.cpp
    CompileOptionsDialog.cpp
    BuildOutputDialog.cpp
    HelpDialog.cpp
    NewProjectDialog.cpp
    AddFileDialog.cpp
    GediProject.cpp
    ProjectPropertiesDialog.cpp
    TargetDialog.cpp
    PickTargetDialog.cpp
    utils.cpp
    DialogBase.cpp
    DependencyChecker.cpp
    platform_compat.cpp
    debugger/DebuggerFactory.cpp
    debugger/GdbDebugger.cpp
)

# Helper that copies the runtime data files next to a freshly built target.
function(copy_data_files target)
    foreach(_data_file ${ARGN})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/${_data_file}
                $<TARGET_FILE_DIR:${target}>/${_data_file}
            COMMENT "Copying ${_data_file} to ${target} build directory")
    endforeach()
endfunction()

# ===========================================================================
#  Target 1: gedi  — the classic text-mode (ncurses) application
# ===========================================================================
set(CURSES_NEED_WIDE TRUE)
find_package(Curses)
if(CURSES_FOUND)
    add_executable(gedi ${GEDI_COMMON_SOURCES})
    target_include_directories(gedi PRIVATE ${CURSES_INCLUDE_DIRS} ${CLANG_INCLUDE_DIR})
    target_link_libraries(gedi ${CURSES_LIBRARIES} ${CLANG_LIBRARY})
    copy_data_files(gedi colors.json config.json help.hlp)
    install(TARGETS gedi DESTINATION bin)
else()
    message(WARNING "ncurses not found — skipping the text-mode 'gedi' target.")
endif()

# ===========================================================================
#  Target 2: gedi-gui — the graphical (SDL) application
#
#  Re-uses every shared source but swaps the ncurses backend for the
#  curses_compat emulation layer running on top of the SDL VgaTextEngine.
# ===========================================================================
if(NOT SDL2_FOUND)
    find_package(SDL2)
endif()
if(SDL2_FOUND)
    add_executable(gedi-gui
        ${GEDI_COMMON_SOURCES}
        curses_compat.cpp
        graphics_engine/VgaTextEngine.cpp
    )
    target_compile_definitions(gedi-gui PRIVATE GEDI_GUI)
    target_include_directories(gedi-gui PRIVATE ${CLANG_INCLUDE_DIR} ${SDL2_INCLUDE_DIRS})
    # 'util' provides openpty/forkpty for the Linux pty-backed build; it does
    # not exist on Windows, so only link it off-Windows.
    if(WIN32)
        target_link_libraries(gedi-gui ${CLANG_LIBRARY} ${SDL2_LIBRARIES})
    else()
        target_link_libraries(gedi-gui ${CLANG_LIBRARY} ${SDL2_LIBRARIES} util)
    endif()

    # Optional SDL2_image: enables loading the About-box logo (a PNG). When it is
    # not available (e.g. the vendored Windows SDL2 does not ship it) the About box
    # still works — it just omits the image (or can load a .bmp via core SDL).
    find_library(SDL2_IMAGE_LIBRARY NAMES SDL2_image)
    find_path(SDL2_IMAGE_INCLUDE_DIR NAMES SDL2/SDL_image.h HINTS ${SDL2_INCLUDE_DIRS})
    if(SDL2_IMAGE_LIBRARY AND SDL2_IMAGE_INCLUDE_DIR)
        target_compile_definitions(gedi-gui PRIVATE GEDI_HAVE_SDL_IMAGE)
        target_include_directories(gedi-gui PRIVATE ${SDL2_IMAGE_INCLUDE_DIR})
        target_link_libraries(gedi-gui ${SDL2_IMAGE_LIBRARY})
        message(STATUS "SDL2_image found — About-box logo (PNG) enabled.")
    else()
        message(STATUS "SDL2_image not found — About-box logo will be omitted.")
    endif()

    # It's a graphical app: run it under the Windows subsystem so no console
    # window is allocated. The code is written against a plain int main(argc,argv),
    # so keep that as the actual entry point (mainCRTStartup) rather than requiring
    # a WinMain — only the subsystem (no console) changes.
    if(WIN32)
        set_target_properties(gedi-gui PROPERTIES WIN32_EXECUTABLE TRUE)
        if(MSVC)
            set_target_properties(gedi-gui PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
        endif()
    endif()
    copy_data_files(gedi-gui colors.json config.json help.hlp icons/gedi_1.0-icon.png)
    # The VGA fonts live in graphics_engine/; copy them next to the binary.
    add_custom_command(TARGET gedi-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/graphics_engine/VGA9.F16
            $<TARGET_FILE_DIR:gedi-gui>/VGA9.F16
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/graphics_engine/SMVGA.F16
            $<TARGET_FILE_DIR:gedi-gui>/SMVGA.F16
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/fonts
            $<TARGET_FILE_DIR:gedi-gui>/fonts
        COMMENT "Copying VGA fonts to gedi-gui build directory")
    install(TARGETS gedi-gui DESTINATION bin)
    install(FILES graphics_engine/VGA9.F16 DESTINATION share/gedi)
    install(FILES icons/gedi_1.0-icon.png DESTINATION share/gedi/icons)

    # Windows has no system libclang/SDL2 — drop the vendored DLLs next to the
    # freshly built executable so it runs without extra PATH setup.
    if(WIN32)
        if(CLANG_DLL)
            add_custom_command(TARGET gedi-gui POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CLANG_DLL} $<TARGET_FILE_DIR:gedi-gui>
                COMMENT "Copying libclang.dll to gedi-gui build directory")
        endif()
        foreach(_dll ${SDL2_RUNTIME_DLLS})
            if(EXISTS ${_dll})
                add_custom_command(TARGET gedi-gui POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_dll} $<TARGET_FILE_DIR:gedi-gui>
                    COMMENT "Copying ${_dll} to gedi-gui build directory")
            endif()
        endforeach()
    endif()
else()
    message(WARNING "SDL2 not found — skipping the graphical 'gedi-gui' target. Install libsdl2-dev to enable it.")
endif()

# ---------------------------------------------------------------------------
#  Shared install rules (data + helper scripts)
# ---------------------------------------------------------------------------
install(
    FILES help.hlp colors.json config.json librarian.py
    DESTINATION share/gedi
)
install(
    FILES cguess.py
    DESTINATION lib/python3/dist-packages/${PROJECT_NAME}
)
