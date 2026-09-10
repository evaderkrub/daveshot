#!/usr/bin/env bash
# Configures, builds and tests daveshot on Linux or macOS.
#
#   ./scripts/build.sh                 # debug: configure, build, test
#   ./scripts/build.sh release
#   ./scripts/build.sh debug --skip-tests
#
# Build output goes to ~/buildfiles/daveshot/<platform>-<preset>, never into the
# source tree. The finished program is the stage/ folder inside it.
#
# Needs: cmake 3.24+, ninja, a C++20 compiler, and the development packages
# for X11, D-Bus, Wayland and libdecor -- on Debian and Ubuntu:
#   sudo apt install build-essential cmake ninja-build pkg-config \
#        libx11-dev libxext-dev libdbus-1-dev libwayland-dev wayland-protocols \
#        libxkbcommon-dev libdecor-0-dev libegl-dev libgl-dev
set -euo pipefail

# Optional isolated tool install documented in the README.
tool_dir="$HOME/buildfiles/daveshot/build-tools/bin"
if ! command -v cmake >/dev/null 2>&1 && [[ -x "$tool_dir/cmake" ]]; then
    export PATH="$tool_dir:$PATH"
fi

preset="${1:-debug}"
skip_tests=0
for arg in "$@"; do
    [[ "$arg" == "--skip-tests" ]] && skip_tests=1
done
case "$preset" in
    debug|release) ;;
    --skip-tests) preset=debug ;;
    *) echo "usage: $0 [debug|release] [--skip-tests]" >&2; exit 2 ;;
esac

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

platform=linux
[[ "$(uname -s)" == Darwin ]] && platform=macos

cmake --preset "$platform-$preset"
cmake --build --preset "$platform-$preset"
if [[ "$skip_tests" -eq 0 ]]; then
    ctest --preset "$platform-$preset"
fi

echo
echo "Staged build: $HOME/buildfiles/daveshot/$platform-$preset/stage"
