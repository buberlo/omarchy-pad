#!/bin/sh
# Builds the guest session binaries for the **Arch (glibc)** Omarchy Pad root:
#
#   ishwl, ishwl-x11-wm, libishwl-scm.so, libishwl-compat.so   from wl-bridge/
#   omarchy-pad-bridge                                          from omarchy-pad/bridge/
#
# Why a separate builder: the binaries the Alpine root ships are musl-linked, and a glibc
# guest must not load them (and the musl-specific LD_PRELOAD shims are wrong there — the
# Arch probe of SCM_RIGHTS fd passing without a preload is clean). The same sources are
# built against Arch's glibc inside the aarch64 colima VM.
#
#   sh installation/omarchy/build-guest-tools.sh [outdir]
#
# Output defaults to <repo>/work/omarchy-pad/guest-tools/arch, with a NEEDED listing per
# binary so linkage can be checked before anything is installed into an image.
#
# There is no official arm64 Arch image (archlinux:latest is x86_64 only), so this uses the
# community `menci/archlinuxarm` image. Bind mounts are not shared into the colima VM, so
# sources and artifacts travel with docker cp.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${1:-$ROOT/work/omarchy-pad/guest-tools/arch}
CTX=${CTX:-colima-linpad-fex}
IMAGE=${IMAGE:-linpad-ishwl-builder:archlinuxarm}
BASE_IMAGE=${BASE_IMAGE:-menci/archlinuxarm:latest}
DOCKER="docker --context $CTX"
# Arch equivalents of the Alpine list in wl-bridge/Makefile: libx11 for ishwl-x11's XKB
# handling; zlib and libxkbcommon are the real link dependencies. The bridge adds the
# Wayland client protocols it generates at build time (wayland + wayland-protocols).
DEPS="base-devel wayland wayland-protocols pkgconf zlib libxkbcommon libx11"

$DOCKER info >/dev/null 2>&1 || {
    echo "$0: the docker context $CTX is not usable (colima start linpad-fex)" >&2
    exit 2
}

# One reusable image with the build dependencies: pacman is the slow part, so it runs once.
if ! $DOCKER image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "preparing $IMAGE from $BASE_IMAGE (pacman -Sy: $DEPS)"
    id=$($DOCKER create "$BASE_IMAGE" sh -c 'while :; do sleep 3600; done')
    $DOCKER start "$id" >/dev/null
    # pacman's Landlock sandbox cannot work in this container, and cannot work on the iPad
    # either (iSH stubs landlock_*), so it is off in [options] and on the command line.
    $DOCKER exec "$id" sh -c "sed -i 's/^DownloadUser/#DownloadUser/' /etc/pacman.conf; sed -i '/^\[options\]/a DisableSandbox' /etc/pacman.conf"
    $DOCKER exec "$id" sh -c 'pacman-key --init 2>/dev/null || true; pacman-key --populate archlinuxarm 2>/dev/null || true'
    $DOCKER exec "$id" sh -c "pacman -Syu --noconfirm --disable-sandbox && pacman -S --noconfirm --needed --disable-sandbox $DEPS"
    $DOCKER exec "$id" sh -c 'pacman -Scc --noconfirm' >/dev/null 2>&1 || true
    $DOCKER commit "$id" "$IMAGE" >/dev/null
    $DOCKER rm -f "$id" >/dev/null
fi

mkdir -p "$OUT"
id=$($DOCKER create "$IMAGE" sh -c 'while :; do sleep 3600; done')
trap '$DOCKER rm -f "$id" >/dev/null' EXIT INT TERM
$DOCKER start "$id" >/dev/null
$DOCKER exec "$id" sh -c 'rm -rf /wb /pad /out && mkdir -p /wb /pad /out'
$DOCKER cp "$ROOT/wl-bridge/." "$id:/wb"

echo "building ishwl"
$DOCKER exec "$id" sh -c 'cd /wb && make -j4' 2>&1 | tail -5
$DOCKER exec "$id" sh -c 'cd /wb && cp -f ishwl libishwl-scm.so libishwl-compat.so ishwl-x11-wm /out/ 2>/dev/null || true'

if [ -d "$ROOT/omarchy-pad/bridge" ]; then
    echo "building omarchy-pad-bridge"
    $DOCKER cp "$ROOT/omarchy-pad/bridge/." "$id:/pad"
    $DOCKER exec "$id" sh -c 'cd /pad && make -j4' 2>&1 | tail -5
    $DOCKER exec "$id" sh -c 'cp -f /pad/omarchy-pad-bridge /out/ 2>/dev/null || true'
else
    echo "omarchy-pad/bridge does not exist yet; skipping the bridge"
fi

$DOCKER exec "$id" sh -c 'for f in /out/*; do echo "--- $f"; readelf -d "$f" | grep NEEDED || true; done' >"$OUT/NEEDED.txt" 2>&1 || true
$DOCKER cp "$id:/out/." "$OUT/"

echo
cat "$OUT/NEEDED.txt" 2>/dev/null || true
echo "=== built into $OUT ==="
ls -la "$OUT"
