#!/bin/sh
set -eu
export ZIG_GLOBAL_CACHE_DIR="$PWD/work/zig-cache"
base=work/performance/mono-init-probe
dest=work/performance/vulkan-repro-root/opt/mono-init
work/build-tools/bin/python -m ziglang cc -target x86_64-linux-gnu -O2 -Wall -Wextra -Werror "$base/probe.c" -ldl -o "$dest/probe"
work/build-tools/bin/python -m ziglang cc -target aarch64-linux-musl -static -O2 -Wall -Wextra -Werror "$base/launch.c" -o "$dest/launch"
cp work/performance/game-fault-report/libmonobdwgc-2.0.so "$dest/libmonobdwgc-2.0.so"
