#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
export ZIG_GLOBAL_CACHE_DIR="$PWD/work/zig-cache"
work/build-tools/lib/python3.15/site-packages/ziglang/zig cc -target x86_64-linux-gnu -O2 -Wall -Wextra \
    work/performance/audio-path-audit/pulse-connect-probe.c -ldl -o work/performance/audio-path-audit/pulse-connect-probe
