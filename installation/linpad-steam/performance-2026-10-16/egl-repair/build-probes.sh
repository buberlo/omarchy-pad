#!/bin/sh
set -eu
# Run from the session workspace root.
export ZIG_GLOBAL_CACHE_DIR="$PWD/work/zig-cache"
work/build-tools/bin/python -m ziglang cc -target x86_64-linux-gnu -O2 -Wall -Wextra -Werror \
    work/performance/egl-display-audit/egl-display-probe.c -ldl \
    -o work/performance/serve/egl-display-probe
work/build-tools/bin/python -m ziglang cc -target x86_64-linux-gnu -O2 -Wall -Wextra -Werror \
    work/performance/egl-display-audit/sdl-window-probe.c -ldl \
    -o work/performance/serve/sdl-window-probe
