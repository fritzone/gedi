cmake_minimum_required(VERSION 3.10)
project(gedi CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
#  Dependencies common to both flavours
# ---------------------------------------------------------------------------
# libclang (semantic highlighting / go-to-definition)
find_path(CLANG_INCLUDE_DIR clang-c/Index.h)
find_library(CLANG_LIBRARY NAMES clang libclang)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Clang DEFAULT_MSG CLANG_LIBRARY CLANG_INCLUDE_DIR)
if(NOT CLANG_FOUND)
    message(FATAL_ERROR "libclang not found. Please install libclang-dev or equivalent.")
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
    KeyBindings.cpp
    MessageDialog.cpp
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
#  curses_compat emulation layer running on top of the SDL BorlandEngine.
# ===========================================================================
find_package(SDL2)
if(SDL2_FOUND)
    add_executable(gedi-gui
        ${GEDI_COMMON_SOURCES}
        curses_compat.cpp
        graphics_engine/BorlandEngine.cpp
    )
    target_compile_definitions(gedi-gui PRIVATE GEDI_GUI)
    target_include_directories(gedi-gui PRIVATE ${CLANG_INCLUDE_DIR} ${SDL2_INCLUDE_DIRS})
    target_link_libraries(gedi-gui ${CLANG_LIBRARY} ${SDL2_LIBRARIES})
    copy_data_files(gedi-gui colors.json config.json help.hlp)
    # The VGA font lives in graphics_engine/; copy it next to the binary.
    add_custom_command(TARGET gedi-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/graphics_engine/VGA9.F16
            $<TARGET_FILE_DIR:gedi-gui>/VGA9.F16
        COMMENT "Copying VGA9.F16 to gedi-gui build directory")
    install(TARGETS gedi-gui DESTINATION bin)
    install(FILES graphics_engine/VGA9.F16 DESTINATION share/gedi)
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
