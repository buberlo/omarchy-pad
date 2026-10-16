#!/bin/sh
set -eu
git config --global --add safe.directory /work/FEX
cmake -S /work/FEX -B /work/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-18 \
  '-DCMAKE_EXE_LINKER_FLAGS=-static-pie -fuse-ld=lld' \
  -DBUILD_FEXCONFIG=False -DENABLE_CCACHE=False \
  -DENABLE_OFFLINE_TELEMETRY=False -DENABLE_GDB_SYMBOLS=False \
  -DTUNE_CPU=apple-m1
cmake --build /work/build --target FEX --parallel 4
cp /work/build/Bin/FEX /work/FEX-apple-m1-munmap
file /work/FEX-apple-m1-munmap
sha256sum /work/FEX-apple-m1-munmap
