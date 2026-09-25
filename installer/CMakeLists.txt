# ===========================================================================
#  installer - a retro, Turbo-style setup builder for the Windows build
#
#  Two programs and one product:
#
#    setup-stub       the installer front end. Draws on the very same VGA text
#                     engine gedi runs on, so setup looks like the editor.
#    gedi-mkinstall   the build tool: reads install.yaml, packs everything it
#                     names, and welds the result onto setup-stub.
#    gedi-setup       the custom target that runs the two together and drops
#                     gedi-setup.exe next to the editor.
#
#  Windows only: the stub speaks Win32 for shortcuts, the registry and the
#  compression API. gedi-mkinstall itself would build anywhere, but it is only
#  useful where a stub exists to weld onto.
# ===========================================================================

if(NOT WIN32)
    message(STATUS "installer: skipped (Windows only)")
    return()
endif()
if(NOT SDL2_FOUND)
    message(WARNING "installer: skipped - the setup UI needs the same SDL2 as gedi-gui")
    return()
endif()

set(GEDI_ROOT ${CMAKE_SOURCE_DIR})

# ---------------------------------------------------------------------------
#  Shared between builder and stub
# ---------------------------------------------------------------------------
set(INSTALLER_COMMON_SOURCES
    common/Yaml.cpp
    common/Archive.cpp
)

# ---------------------------------------------------------------------------
#  The pieces of gedi the setup UI reuses.
#
#  Renderer, the widget/dialog framework and the file browser sit on top of
#  curses_compat, which sits on VgaTextEngine - the whole Borland look comes
#  along without a line of it being duplicated here.
# ---------------------------------------------------------------------------
set(GEDI_UI_SOURCES
    ${GEDI_ROOT}/Renderer.cpp
    ${GEDI_ROOT}/DialogBase.cpp
    ${GEDI_ROOT}/FileBrowser.cpp
    ${GEDI_ROOT}/utils.cpp
    ${GEDI_ROOT}/platform_compat.cpp
    ${GEDI_ROOT}/curses_compat.cpp
    ${GEDI_ROOT}/graphics_engine/VgaTextEngine.cpp
)

# ---------------------------------------------------------------------------
#  Target 1: setup-stub
# ---------------------------------------------------------------------------
add_executable(setup-stub
    stub/main.cpp
    stub/Wizard.cpp
    stub/Payload.cpp
    stub/Install.cpp
    setup.rc                 # the installer's icon; inherited by the uninstaller
    ${INSTALLER_COMMON_SOURCES}
    ${GEDI_UI_SOURCES}
)

# GEDI_GUI picks the SDL backend in curses_compat. GEDI_HAVE_SDL_IMAGE is
# deliberately NOT set: keeping SDL2_image out of the stub means the bootstrap
# only has to unpack SDL2.dll itself, not a PNG decoder and its two libraries.
# NOMINMAX: windows.h defines max/min as macros, which collide with std::max in
# both our code and <filesystem>.
target_compile_definitions(setup-stub PRIVATE GEDI_GUI NOMINMAX)
target_include_directories(setup-stub PRIVATE
    ${GEDI_ROOT}
    ${GEDI_ROOT}/graphics_engine
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(setup-stub PRIVATE
    ${SDL2_LIBRARIES}
    Cabinet.lib      # the LZMS compressor used for the payload
    ole32.lib        # IShellLink, for the shortcuts
    shell32.lib
    uuid.lib
    advapi32.lib     # the Add/Remove Programs registry entry
)

set_target_properties(setup-stub PROPERTIES WIN32_EXECUTABLE TRUE)
if(MSVC)
    # As for gedi-gui: a real main(), just no console window.
    #
    # /DELAYLOAD is the load-bearing flag here. SDL2.dll travels inside the
    # payload, so it cannot be resolved when the process starts - the stub has to
    # run far enough to unpack it first. Delay-loading defers the bind to the
    # first SDL call, by which time SetDllDirectory points at the scratch copy.
    set_target_properties(setup-stub PROPERTIES
        LINK_FLAGS "/ENTRY:mainCRTStartup /DELAYLOAD:SDL2.dll")
    target_link_libraries(setup-stub PRIVATE delayimp.lib)
endif()

# ---------------------------------------------------------------------------
#  Target 2: gedi-mkinstall
# ---------------------------------------------------------------------------
add_executable(gedi-mkinstall
    builder/main.cpp
    ${INSTALLER_COMMON_SOURCES}
)
target_compile_definitions(gedi-mkinstall PRIVATE NOMINMAX)
target_include_directories(gedi-mkinstall PRIVATE ${GEDI_ROOT})
target_link_libraries(gedi-mkinstall PRIVATE Cabinet.lib)

# ---------------------------------------------------------------------------
#  Target 3: gedi-setup - the finished installer
#
#  --root is gedi-gui's output directory because the editor's POST_BUILD steps
#  have already gathered every payload file there: the exe, the vendored DLLs,
#  colors/config/help, the fonts and the build templates.
# ---------------------------------------------------------------------------
if(TARGET gedi-gui)
    set(GEDI_SETUP_EXE $<TARGET_FILE_DIR:gedi-gui>/gedi-setup.exe)

    add_custom_target(gedi-setup
        COMMAND $<TARGET_FILE:gedi-mkinstall>
                ${CMAKE_CURRENT_SOURCE_DIR}/install.yaml
                --root  $<TARGET_FILE_DIR:gedi-gui>
                --stub  $<TARGET_FILE:setup-stub>
                --output ${GEDI_SETUP_EXE}
        DEPENDS gedi-mkinstall setup-stub gedi-gui
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Building gedi-setup.exe from install.yaml"
        VERBATIM)
else()
    message(STATUS "installer: gedi-gui is not being built, so the gedi-setup target is unavailable")
endif()
