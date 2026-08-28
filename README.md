# daveshot

A native desktop screen-capture tool for Windows: C++20, SDL3 for the window
and input, Dear ImGui (docking branch) for the interface.

**This repository is currently the skeleton, not the product.** A window
opens, the interface responds, the build produces a folder you can copy to
another machine, and the test binaries pass. Capture itself is the next piece
of work — the `Capture` button and its menu item are present and deliberately
inert.

What is here today:

- A dockable interface: a workspace panel, a settings panel, an always-modal
  About box, and ImGui's demo window for reference while building.
- Five themes built from fwcom's 17-token colour system, switchable at
  runtime.
- A UI scale control, from 0.75x to 3x, applied on top of the display's own
  DPI scale. Both style metrics and text scale together.
- Settings (theme and scale) persisted to `daveshot_settings.txt` beside the
  executable.
- Open Sans for UI text with Material Icons merged in, and Fira Code for
  fixed-width text — all loaded from `assets/` beside the executable.
- Two console test binaries, one of which drives the real interface through
  the Dear ImGui Test Engine.

## Building

Requires CMake 3.24+, Ninja, and Visual Studio 2022 or newer with the C++
toolset. SDL3, Dear ImGui and the ImGui Test Engine are fetched by CMake at
configure time (pinned tags — see `cmake/Dependencies.cmake`); nothing else
needs installing.

```powershell
./scripts/build.ps1                  # debug: configure, build, run tests
./scripts/build.ps1 -Preset release
```

The script imports the MSVC environment before running CMake. That step is not
cosmetic: Ninja does not set the toolchain up the way the Visual Studio
generator does, and without it CMake will happily pick up some other compiler
on the machine — an msys2 `g++`, say — and produce a build that is neither
`/W4`-clean nor statically linked against the MSVC runtime.

If you already have a Developer PowerShell open, the presets work directly:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Build output goes to `C:/buildfiles/daveshot/<preset>`, never into the source
tree. The finished program is the `stage` folder inside it:

```
C:/buildfiles/daveshot/release/stage/
    daveshot.exe
    assets/fonts/...
    assets/icons/...
```

Copy that folder anywhere and run it. The executable links SDL3, ImGui and the
CRT statically, so it depends on nothing but Windows' own DLLs, and every
runtime path is resolved against the executable's own directory rather than
the working directory — launching from a shortcut or another drive gives a
different working directory, and that is the usual way a portable build stops
being portable.

## Testing

```powershell
ctest --preset debug        # both binaries
```

or run them directly out of the stage folder:

- `daveshot_test_app.exe` — the state layer with no window and no ImGui
  context: settings parsing and clamping, the file round trip, path
  resolution, and the rule that decides when the style needs rebuilding.
- `daveshot_test_ui.exe` — end-to-end tests driven by the Dear ImGui Test
  Engine against a real ImGui context with no window and no GPU. It clicks
  through the actual `ui::Draw` the application runs: opening the About box
  and asserting it really is modal, changing the UI scale and checking both
  the metrics and the text moved, switching themes, and quitting from the
  menu.

Both print a one-line summary and return non-zero on failure.

## Layout

```
CMakeLists.txt
CMakePresets.json     debug/release, Ninja, output under C:/buildfiles
cmake/                dependency fetching, warning flags, version header
scripts/build.ps1     MSVC environment + configure + build + test
src/main.cpp          entry point, nothing else
src/app/              application state and logic, no ImGui calls
src/ui/               everything that draws
src/platform/         anything that touches the OS directly
assets/               fonts, icons, licences
tests/                console test binaries
```

`src/ui/AppLoop.cpp` is the composition root — it opens the window, loads
settings and fonts, and runs the frame loop. It sits under `src/ui` because it
exists to drive the drawing; that is what keeps `main.cpp` a single line and
`src/app` free of ImGui.

## Conventions

- `/W4 /permissive- /WX` under MSVC. Third-party headers are included as
  system headers so their warnings cannot fail our build, and ours are not
  silenced.
- The drawing code reads application state and does not own it. State lives in
  plain structs in `src/app` that the console tests exercise without a window.
- No exceptions across module boundaries. Failures return `false` plus an
  error string the interface can show; `AppState::error` is drained into a
  modal.
- Comments explain why a thing is the way it is, not what the line does.

## Copied from fwcom

The look is carried over from `fwcom` rather than reinvented:

- `src/ui/Theme.cpp` — the 17-token theme system and its palette derivation,
  ported from `fwTheme.cpp`. ImPlot theming and fwcom's legacy settings-key
  mapping were dropped; the "Wili Dark" palette is kept as one of the themes.
- `src/ui/IconsMaterialDesign.h` — the Material Icons name constants,
  unchanged.
- `assets/fonts`, `assets/icons` — Open Sans, Fira Code and Material Icons,
  the same files fwcom uses, with Open Sans' OFL licence alongside them.

fwcom's FreeWili branding — the logo images and application icon — was
deliberately not copied, since this is a different application.
