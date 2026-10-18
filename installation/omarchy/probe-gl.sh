#!/bin/sh
# Cross-compiles the GL probes and runs them inside the CLI guest, under zink and under
# llvmpipe, recording what it saw under <WORK>/evidence/gate-0.3/.
#
#   sh installation/omarchy/probe-gl.sh [fakefs]
#
# Why this exists: the guest's GL is the one thing the emulator cannot be asked about from the
# Mac, and two Hyprland aborts have now come from it ("Framebuffer incomplete ... GL Error:
# 0x505" on the first frame, which turned out to be depth/stencil storage the driver would not
# give). One probe checks the whole path a compositor uses (surfaceless EGL, GLES 3.0 context,
# RGBA8 render target, clear, readback); the other enumerates which colour/depth-stencil
# attachment combinations are complete, because that is what differs between drivers.
#
# The probes are linked against the guest's own libEGL/libGLESv2 and run in the guest, so the
# answer is about the guest's drivers, not the Mac's.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)          # the LinPad checkout
WS=$(cd "$ROOT/../.." && pwd)            # the workspace that holds work/
WORK=${WORK:-$WS/work/omarchy-pad}
FS=${1:-$ROOT/work/omarchy-pad/fs-omarchy-aarch64-pad}
ISH=${ISH:-$ROOT/build-arm64-release-gpu/ish}
ZIG=${ZIG:-$WS/work/tools/venv/lib/python3.15/site-packages/ziglang/zig}
EVIDENCE=$WORK/evidence/gate-0.3/framebuffer-probe.txt

[ -d "$FS/data" ] || { echo "$0: no fakefs at $FS (import the image with installation/omarchy/boot-test.sh)" >&2; exit 2; }
[ -x "$ISH" ] || { echo "$0: no CLI emulator at $ISH" >&2; exit 2; }
[ -x "$ZIG" ] || { echo "$0: no zig at $ZIG" >&2; exit 2; }

# The GL headers come from the guest; libc comes from zig (mixing the guest's glibc headers with
# zig's breaks the build, and the guest's headers are only needed for EGL/GLES).
inc=$(mktemp -d)
for d in EGL GLES3 GLES2 KHR; do
    [ -d "$FS/data/usr/include/$d" ] && ln -s "$FS/data/usr/include/$d" "$inc/$d"
done
trap 'rm -rf "$inc"' EXIT

# The real libraries, not the resolver's placeholder files: installation/omarchy/
# resolve-case-collisions.sh turns case-variant symlinks into regular files containing the name
# they point at, so libEGL.so is 11 bytes of text and the linker finds no symbols in it.
EGL=$(ls "$FS"/data/usr/lib/libEGL.so.1.1.0 2>/dev/null | head -1)
GLES=$(ls "$FS"/data/usr/lib/libGLESv2.so.2.1.0 2>/dev/null | head -1)
[ -n "$EGL" ] && [ -n "$GLES" ] || { echo "$0: the guest has no libEGL.so.1.1.0/libGLESv2.so.2.1.0" >&2; exit 2; }

mkdir -p "$WORK/evidence/gate-0.3"
for probe in glprobe:probe-gl.c glprobe2:probe-gl-attachments.c; do
    out=${probe%%:*}
    src=$HERE/${probe##*:}
    "$ZIG" cc -target aarch64-linux-gnu -O1 -I"$inc" -o "/tmp/$out" "$src" "$EGL" "$GLES" 2>&1 |
        grep -vE "warning|note" || true
    [ -x "/tmp/$out" ] || { echo "$0: $src did not build" >&2; exit 1; }
    cp "/tmp/$out" "$FS/data/tmp/$out"
    python3 "$HERE/hyprland-pad/install-into-fakefs.py" --fs "$FS" --mode 755 --register "/tmp/$out" >/dev/null
done

PROBE='export XDG_RUNTIME_DIR=/tmp/xdg-runtime HOME=/root MESA_VK_WSI_DEBUG=sw MESA_GLES_VERSION_OVERRIDE=3.0
for icd in /usr/share/vulkan/icd.d/virtio_icd.json /usr/share/vulkan/icd.d/virtio_icd.aarch64.json; do
    [ -r "$icd" ] && { export VK_ICD_FILENAMES=$icd; break; }
done
echo "== the GPU path: zink on Venus"
echo "VK_ICD_FILENAMES=$VK_ICD_FILENAMES"
MESA_LOADER_DRIVER_OVERRIDE=zink /tmp/glprobe 1024 768 2>&1
echo
echo "== attachment combinations under zink (the compositor needs a render target)"
MESA_LOADER_DRIVER_OVERRIDE=zink /tmp/glprobe2 800 600 2>&1
echo
echo "== the same, forced onto the software rasteriser"
MESA_LOADER_DRIVER_OVERRIDE=llvmpipe /tmp/glprobe2 800 600 2>&1
echo PROBE-END'

{
    echo "== GPU probes, $(date -u +%Y-%m-%dT%H:%M:%SZ), image $(cat "$WORK"/omarchy-aarch64-pad.version 2>/dev/null || echo unknown)"
    "$ISH" -f "$FS" /bin/sh -lc "$PROBE" 2>&1
} | tee "$EVIDENCE" | grep -E "GL_RENDERER|RESULT|mode [0-9]|status with|storage=|VK_ICD|PROBE-END" | head -30

grep -q "RESULT: FRAMEBUFFER-OK" "$EVIDENCE" || { echo "$0: the GPU path did not render a frame (see $EVIDENCE)" >&2; exit 1; }
echo "ok: $EVIDENCE"
