#!/bin/sh
# Rebuild the authored diagnostics; archived binaries retain their recorded hashes.
# Invoke from the original workspace root; output defaults to a new scratch path.
set -eu
source_dir=${LINPAD_SAMPLE_SOURCE_DIR:-work/performance/mono-stack-repair/sampler}
output_dir=${LINPAD_SAMPLE_BUILD_DIR:-work/performance/sampler-rebuild}
export ZIG_GLOBAL_CACHE_DIR="$PWD/work/zig-cache"
mkdir -p "$output_dir"
work/build-tools/bin/python -m ziglang cc -target x86_64-linux-gnu -O2 -fPIC -shared "$source_dir/x86-thread-sample.c" -ldl -pthread -o "$output_dir/x86-thread-sample.so"
work/build-tools/bin/python -m ziglang cc -target aarch64-linux-musl -static -O2 "$source_dir/thread-sample-send.c" -o "$output_dir/thread-sample-send"
work/build-tools/bin/python -m ziglang cc -target x86_64-linux-gnu -O2 "$source_dir/thread-sample-probe.c" -pthread -o "$output_dir/thread-sample-probe"
work/build-tools/bin/python -m ziglang cc -target aarch64-linux-musl -static -O2 "$source_dir/thread-sample-launch.c" -o "$output_dir/thread-sample-launch"
