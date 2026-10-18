#!/bin/sh
# Start Left 4 Dead the direct way (Wine, no Steam DRM handshake).
# Sets the environment the X wrapper needs -- in a fresh shell these are unset, which is why
# ishwl-x11 produced no output and /tmp/l4d-game.log stayed empty.
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
export WAYLAND_DISPLAY=wayland-0
mkdir -p "$XDG_RUNTIME_DIR"
echo "env: XDG=$XDG_RUNTIME_DIR WAY=$WAYLAND_DISPLAY"
[ -x /usr/local/bin/ishwl-x11 ] || { echo "missing /usr/local/bin/ishwl-x11" >&2; exit 80; }
echo "starting l4d-play -g ..."
exec /usr/local/bin/l4d-play -g
