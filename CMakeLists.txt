cmake_minimum_required(VERSION 3.24)

project(daveshot
        VERSION 0.1.0
        DESCRIPTION "Screen capture tool"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT. The brief is a folder you can copy to another machine and run,
# so nothing may depend on a VC++ redistributable being installed there.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# One folder holds the executables and, after the staging step below, every
# asset they open at runtime. That folder is the deliverable.
set(DAVESHOT_STAGE_DIR "${CMAKE_BINARY_DIR}/stage")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${DAVESHOT_STAGE_DIR}")
foreach(cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} "${DAVESHOT_STAGE_DIR}")
endforeach()

configure_file(cmake/Version.h.in
               "${CMAKE_BINARY_DIR}/generated/daveshot/Version.h" @ONLY)

include(cmake/Dependencies.cmake)
include(cmake/Warnings.cmake)

# ---------------------------------------------------------------------------
# daveshot_core -- everything except the entry point, so the test binaries can
# link the same code the application runs.
# ---------------------------------------------------------------------------
add_library(daveshot_core STATIC
    src/app/AppState.cpp
    src/app/Capture.cpp
    src/app/CaptureFlow.cpp
    src/app/CaptureService.cpp
    src/app/Image.cpp
    src/app/Naming.cpp
    src/app/Settings.cpp

    src/platform/Clipboard.cpp
    src/platform/Host.cpp
    src/platform/Hotkeys.cpp
    src/platform/ImageIo.cpp
    src/platform/Paths.cpp
    src/platform/Screen.cpp

    src/ui/AboutDialog.cpp
    src/ui/AppLoop.cpp
    src/ui/CapturePanel.cpp
    src/ui/Fonts.cpp
    src/ui/HistoryPanel.cpp
    src/ui/PreviewPanel.cpp
    src/ui/RegionOverlay.cpp
    src/ui/SettingsPanel.cpp
    src/ui/Textures.cpp
    src/ui/Theme.cpp
    src/ui/UiRoot.cpp
)
target_include_directories(daveshot_core PUBLIC src "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(daveshot_core PUBLIC daveshot_imgui SDL3::SDL3-static)

# The capture path is Win32: GDI for reading the screen, DWM for a window's
# real visible bounds, WIC for writing PNG and JPEG without linking an
# encoder, and the shell for known folders and "show in folder".
if(WIN32)
    target_link_libraries(daveshot_core PUBLIC
        dwmapi windowscodecs shell32 shlwapi)
endif()
daveshot_set_warnings(daveshot_core)

# ---------------------------------------------------------------------------
# The application.
# ---------------------------------------------------------------------------
add_executable(daveshot src/main.cpp)
target_link_libraries(daveshot PRIVATE daveshot_core)
daveshot_set_warnings(daveshot)

# GUI subsystem: no console window flashes up when launched from a shortcut.
# SDL3 has no separate main library -- <SDL3/SDL_main.h>, included by main.cpp,
# is what supplies the WinMain that calls our main().
if(WIN32)
    set_target_properties(daveshot PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Assets are staged beside the executable on every build, so running from the
# build tree exercises the same layout that gets copied to another machine.
add_custom_command(TARGET daveshot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "${DAVESHOT_STAGE_DIR}/assets"
    COMMENT "Staging assets beside the executable")

# ---------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# ---------------------------------------------------------------------------
# install() produces the same shape as the stage folder, for packaging.
# ---------------------------------------------------------------------------
install(TARGETS daveshot RUNTIME DESTINATION .)
install(DIRECTORY assets DESTINATION .)
