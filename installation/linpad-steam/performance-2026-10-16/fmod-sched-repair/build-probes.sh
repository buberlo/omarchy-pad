#!/bin/sh
# Run from the workspace root with the existing local Zig compiler.
set -eu
P=${PROBE_DIR:-work/performance/fmod-init-audit}
ZIG=${ZIG:-work/build-tools/lib/python3.15/site-packages/ziglang/zig}
export ZIG_GLOBAL_CACHE_DIR=${ZIG_GLOBAL_CACHE_DIR:-"$PWD/work/zig-cache"}
"$ZIG" cc -target x86_64-linux-gnu -O2 -Wall -Wextra -pthread "$P/sched-priority-probe.c" -o "$P/sched-priority-x86"
"$ZIG" cc -target aarch64-linux-musl -static -O2 -Wall -Wextra -pthread "$P/sched-priority-probe.c" -o "$P/sched-priority-arm64"
"$ZIG" cc -target x86_64-linux-gnu -O2 -Wall -Wextra -pthread "$P/unity-fmod-thread-probe.c" -ldl -o "$P/unity-fmod-thread-probe"
"$ZIG" cc -target aarch64-linux-musl -static -O2 -Wall -Wextra "$P/launch.c" -o "$P/launch"
