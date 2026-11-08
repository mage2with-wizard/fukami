#!/bin/sh
# Fukami (Japanese edition) launcher: use the bundled libraries, then start the game.
# Arguments pass through (e.g. a .chd or .cue path on first launch).
HERE=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Native Wayland: X11 Vulkan presentation fails on this desktop.
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-wayland}"
exec "$HERE/bin/Fukami" "$@"
