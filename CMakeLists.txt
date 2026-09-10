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

if(APPLE)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")
    endif()
    enable_language(OBJC OBJCXX)
endif()

include(cmake/Dependencies.cmake)
include(cmake/Warnings.cmake)

# ---------------------------------------------------------------------------
# The platform layer. Host.cpp and Paths.cpp are shared; everything that
# reads the screen, owns a hotkey, writes an image or talks to the clipboard
# has one file per OS under src/platform/<os>/.
# ---------------------------------------------------------------------------
set(DAVESHOT_PATHS_SOURCE src/platform/Paths.cpp)
if(APPLE)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(DAVESHOT_PATHS_SOURCE src/platform/macos/Paths.mm)
    set(DAVESHOT_PLATFORM_SOURCES src/platform/macos/AppEvents.mm src/platform/macos/Platform.mm src/platform/macos/Screen.mm src/platform/macos/Hotkeys.mm)
elseif(WIN32)
    set(DAVESHOT_PLATFORM_SOURCES
        src/platform/win32/Autostart.cpp
        src/platform/win32/Clipboard.cpp
        src/platform/win32/Hotkeys.cpp
        src/platform/win32/ImageIo.cpp
        src/platform/win32/PrintScreenKey.cpp
        src/platform/win32/Screen.cpp
        src/platform/win32/SingleInstance.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(DAVESHOT_PLATFORM_SOURCES
        src/platform/linux/Autostart.cpp
        src/platform/linux/Clipboard.cpp
        src/platform/linux/Dbus.cpp
        src/platform/linux/Hotkeys.cpp
        src/platform/linux/HotkeysPortal.cpp
        src/platform/linux/HotkeysX11.cpp
        src/platform/linux/ImageCodec.cpp
        src/platform/linux/ImageIo.cpp
        src/platform/linux/PrintScreenKey.cpp
        src/platform/linux/Screen.cpp
        src/platform/linux/ScreenPortal.cpp
        src/platform/linux/ScreenX11.cpp
        src/platform/linux/Session.cpp
        src/platform/linux/SingleInstance.cpp
        src/platform/linux/X11.cpp)
else()
    message(FATAL_ERROR "daveshot builds for Windows, macOS and Linux; ${CMAKE_SYSTEM_NAME} has no platform layer")
endif()

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

    src/platform/Host.cpp
    ${DAVESHOT_PATHS_SOURCE}
    src/platform/Tray.cpp
    ${DAVESHOT_PLATFORM_SOURCES}

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
        dwmapi windowscodecs shell32 shlwapi advapi32)
endif()

# On Linux the screen is read through X11 where there is an X server, and
# through the desktop portal over D-Bus on Wayland. Both libraries are on
# every desktop install, so linking them dynamically costs the portable
# build nothing. Images are encoded by stb, compiled straight in.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
    find_package(Threads REQUIRED)
    target_link_libraries(daveshot_core PUBLIC
        daveshot_stb X11::X11 PkgConfig::DBUS Threads::Threads)
endif()
if(APPLE)
    target_compile_options(daveshot_core PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    target_link_libraries(daveshot_core PUBLIC "-framework Cocoa" "-framework ScreenCaptureKit" "-framework Carbon")
endif()
daveshot_set_warnings(daveshot_core)

# ---------------------------------------------------------------------------
# The application.
# ---------------------------------------------------------------------------
add_executable(daveshot src/main.cpp)
target_link_libraries(daveshot PRIVATE daveshot_core)
daveshot_set_warnings(daveshot)

if(APPLE)
    target_sources(daveshot PRIVATE assets/icons/daveshot.icns)
    set_source_files_properties(assets/icons/daveshot.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(daveshot PROPERTIES MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE daveshot.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER org.daveshot.daveshot
        MACOSX_BUNDLE_BUNDLE_NAME daveshot
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
endif()

# GUI subsystem: no console window flashes up when launched from a shortcut.
# SDL3 has no separate main library -- <SDL3/SDL_main.h>, included by main.cpp,
# is what supplies the WinMain that calls our main().
if(WIN32)
    set_target_properties(daveshot PROPERTIES WIN32_EXECUTABLE TRUE)

    # The icon is compiled into the executable rather than loaded from a file,
    # so the staged folder cannot end up with a program that has lost its icon.
    enable_language(RC)
    target_sources(daveshot PRIVATE src/platform/win32/daveshot.rc)
    set_property(SOURCE src/platform/win32/daveshot.rc APPEND
                 PROPERTY COMPILE_OPTIONS "/I${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# The Linux counterpart of the static CRT: the C++ runtime is the one
# library whose version varies between distributions, so it travels inside
# the executable rather than being looked for on the other machine.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT MSVC)
    foreach(target IN ITEMS daveshot)
        target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
    endforeach()
endif()

# Assets are staged beside the executable on every build, so running from the
# build tree exercises the same layout that gets copied to another machine.
if(APPLE)
    set(DAVESHOT_ASSET_DIR "$<TARGET_BUNDLE_CONTENT_DIR:daveshot>/Resources/assets")
else()
    set(DAVESHOT_ASSET_DIR "${DAVESHOT_STAGE_DIR}/assets")
endif()
add_custom_command(TARGET daveshot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "${DAVESHOT_ASSET_DIR}"
    COMMENT "Staging assets beside the executable")

if(APPLE)
    add_custom_command(TARGET daveshot POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - --identifier org.daveshot.daveshot
                "$<TARGET_BUNDLE_DIR:daveshot>"
        COMMENT "Ad-hoc signing the Mac app bundle")
endif()

# ---------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# ---------------------------------------------------------------------------
# install() produces the same shape as the stage folder, for packaging.
# ---------------------------------------------------------------------------
install(TARGETS daveshot BUNDLE DESTINATION . RUNTIME DESTINATION .)
if(NOT APPLE)
    install(DIRECTORY assets DESTINATION .)
endif()
