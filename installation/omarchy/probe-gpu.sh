#!/bin/sh
# Gate 0.3: what the guest's graphics stack can actually do.
#
# Runs inside the imported fakefs and records, in order:
#   1. the DRM nodes the emulator created (/dev/dri/renderD128 is the only one there is)
#   2. Vulkan: does the Venus ICD enumerate, and on what device
#   3. EGL/GLES through zink on Venus, with and without the GLES version override
#   4. EGL/GLES through llvmpipe (the software fallback)
#
#   sh installation/omarchy/probe-gpu.sh <fakefs-dir> [ish-binary]
#
# Evidence lands in work/omarchy-pad/evidence/gate-0.3/gpu-probe.txt.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
WORK=${WORK:-$ROOT/work/omarchy-pad}
ISH=${2:-${ISH:-$ROOT/build-arm64-release-gpu/ish}}
FS=${1:?usage: probe-gpu.sh FAKEFS [ish]}
OUT=$WORK/evidence/gate-0.3
mkdir -p "$OUT"

PROBE='
echo "## drm nodes"
ls -l /dev/dri 2>&1
echo "## dri sysfs"
ls -l /sys/dev/char/226:128 2>&1 | head -3
echo "## vulkan"
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/virtio_icd.aarch64.json timeout 180 vulkaninfo --summary 2>&1 |
    grep -E "deviceName|driverName|driverInfo|apiVersion|GPU id" | head -8
echo "## zink (no override)"
MESA_LOADER_DRIVER_OVERRIDE=zink timeout 180 eglinfo -B 2>&1 |
    grep -E "OpenGL ES profile (version|renderer|vendor)" | head -6
echo "## zink (GLES 3.0 override)"
MESA_LOADER_DRIVER_OVERRIDE=zink MESA_GLES_VERSION_OVERRIDE=3.0 timeout 180 eglinfo -B 2>&1 |
    grep -E "OpenGL ES profile (version|renderer|vendor)" | head -6
echo "## llvmpipe"
LIBGL_ALWAYS_SOFTWARE=1 MESA_LOADER_DRIVER_OVERRIDE=llvmpipe timeout 180 eglinfo -B 2>&1 |
    grep -E "OpenGL ES profile (version|renderer|vendor)" | head -6
echo "## vulkan via lavapipe (software baseline)"
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.aarch64.json timeout 180 vulkaninfo --summary 2>&1 |
    grep -E "deviceName|driverName" | head -4
echo GPU_PROBE_DONE
'

{
    echo "== gpu probe $(date -u +%Y-%m-%dT%H:%M:%SZ) fs=$FS"
    "$ISH" -f "$FS" /bin/sh -lc "$PROBE"
    echo "== exit $?"
} 2>&1 | tee "$OUT/gpu-probe.txt"

grep -q GPU_PROBE_DONE "$OUT/gpu-probe.txt" || { echo "probe did not finish (see $OUT/gpu-probe.txt)" >&2; exit 1; }
echo "ok: $OUT/gpu-probe.txt"
