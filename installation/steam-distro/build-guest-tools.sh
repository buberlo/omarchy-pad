#!/bin/sh
# Builds LinPad's guest session binaries for the **Arch (glibc)** root from
# work/LinPad/wl-bridge and the ishaudio shim, on this Mac, inside the aarch64
# "linpad-fex" colima VM.
#
# Why a second builder: the guest binaries the Alpine image ships are musl-linked
# (build-ishwl-aarch64.sh builds them in an Alpine 3.21 container on purpose). A glibc guest
# must not load musl binaries, and — more importantly — the musl-specific LD_PRELOAD shims
# are wrong there (see the Phase 0 probe of SCM_RIGHTS fd passing without a preload). So
# this builds the same sources against Arch's glibc.
#
#   sh installation/steam-distro/build-guest-tools.sh [outdir]
#
# Not run yet: Phase 0 (the Arch rootfs spike) is bootstrapping the same distro, and its
# result decides whether the shims are needed at all. Reconcile this script's bootstrap with
# the image Phase 0 produces before relying on it.
#
# Output defaults to workspace `work/steam-distro/guest-tools/arch/`:
#   ishwl, ishwl-x11-wm, libishwl-scm.so, libishwl-compat.so   (from wl-bridge)
# plus `NEEDED` listings per binary so the linkage can be checked before anything is
# installed into an image (`readelf -d`).
#
# There is no official arm64 Arch image (`archlinux:latest` is x86_64 only). Phase 0
# bootstrapped Arch Linux ARM with the community image `menci/archlinuxarm:latest`, so this
# uses that (override BASE_IMAGE to try another) instead of unpacking the ALARM tarball
# again — the same distro, without a second 600 MB download.
#
# Bind mounts are not shared into the colima VM, so sources and artifacts travel with
# docker cp (same technique as build-ishwl-aarch64.sh).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
WS=$(cd "$HERE/../../../.." && pwd)         # the workspace root: it holds work/
SRC="$WS/work/LinPad/wl-bridge"
OUT=${1:-$WS/work/steam-distro/guest-tools/arch}
CTX=${CTX:-colima-linpad-fex}
IMAGE=${IMAGE:-linpad-ishwl-builder:archlinuxarm}
DOCKER="docker --context $CTX"
# Arch equivalents of the Alpine list in wl-bridge/Makefile:2-3. libx11 is for ishwl-x11's
# XKB handling; zlib and libxkbcommon are the real link dependencies.
DEPS="base-devel wayland wayland-protocols pkgconf zlib libxkbcommon libx11"
BASE_IMAGE=${BASE_IMAGE:-menci/archlinuxarm:latest}   # the aarch64 Arch image Phase 0 used

docker_ready() { $DOCKER info >/dev/null 2>&1; }
if ! docker_ready; then
    echo "$0: the docker context $CTX is not usable." >&2
    echo "Start the ARM64 Linux VM first: colima start linpad-fex" >&2
    exit 2
fi

# One reusable image with the build dependencies; pacman is the slow part, so it runs once.
if ! $DOCKER image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "preparing $IMAGE from $BASE_IMAGE (pacman -Sy $DEPS)"
    id=$($DOCKER create "$BASE_IMAGE" sh -c 'while :; do sleep 3600; done')
    $DOCKER start "$id" >/dev/null
    # Arch's pacman sandboxes downloads with Landlock and drops to the alpm user; neither
    # works in a container without those privileges, and neither will work on the iPad
    # (iSH stubs landlock_*). Turn the sandbox off in [options], and pass the flag too.
    $DOCKER exec "$id" sh -c "sed -i 's/^DownloadUser/#DownloadUser/' /etc/pacman.conf; sed -i '/^\\[options\\]/a DisableSandbox' /etc/pacman.conf; grep -A1 '^\\[options\\]' /etc/pacman.conf | head -2"
    # The community image ships a populated keyring; refresh it before installing.
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
$DOCKER exec "$id" sh -c 'rm -rf /wb /out && mkdir -p /wb /out'
$DOCKER cp "$SRC/." "$id:/wb"
$DOCKER exec "$id" sh -c 'cd /wb && make -j4' 2>&1 | tail -20
$DOCKER exec "$id" sh -c 'cd /wb && cp -f ishwl libishwl-scm.so libishwl-compat.so ishwl-x11-wm /out/ 2>/dev/null || true'
$DOCKER exec "$id" sh -c 'for f in /out/*; do echo "--- $f"; readelf -d "$f" | grep NEEDED || true; done' > "$OUT/NEEDED.txt" 2>&1 || true
$DOCKER cp "$id:/out/." "$OUT/"

echo
cat "$OUT/NEEDED.txt" 2>/dev/null || true
echo "=== built into $OUT ==="
ls -l "$OUT" 2>&1
echo
echo "Check before installing: every NEEDED library must come from the Arch image"
echo "(libc.so.6, libwayland-server.so.0, libxkbcommon.so.0, libz.so.1 …), not musl."
