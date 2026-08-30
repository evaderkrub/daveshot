#!/usr/bin/env bash
# Registers a staged daveshot folder with the desktop: writes a .desktop entry
# pointing at that folder's executable into ~/.local/share/applications.
#
#   ./scripts/install-desktop-entry.sh ~/buildfiles/daveshot/linux-release/stage
#
# This is what makes daveshot appear in the application grid, and it is also
# what global hotkeys on GNOME Wayland depend on: the desktop's shortcut
# dialog refuses to bind keys for an application it cannot look up by its
# app id (org.daveshot.daveshot), and the .desktop file is that lookup.
set -euo pipefail

stage="${1:-}"
if [[ -z "$stage" || ! -x "$stage/daveshot" ]]; then
    echo "usage: $0 <stage folder containing daveshot>" >&2
    exit 2
fi
stage="$(cd "$stage" && pwd)"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$apps"
sed -e "s|@EXE@|$stage/daveshot|" -e "s|@ICON@|$stage/assets/icons/daveshot.png|" \
    "$root/assets/linux/org.daveshot.daveshot.desktop" > "$apps/org.daveshot.daveshot.desktop"
update-desktop-database "$apps" 2>/dev/null || true
echo "Wrote $apps/org.daveshot.daveshot.desktop -> $stage/daveshot"
