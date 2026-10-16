#!/bin/sh
# Builds ishwl (and its companion .so files) for the iPad's Alpine 3.21 guest from
# work/LinPad/wl-bridge on this Mac, inside the aarch64 "linpad-fex" colima VM.
#
# The guest builds its own ishwl during the rootfs build (release/build-rootfs.sh runs
# `make CC=tools/ccwrap && make install` on the device). ccwrap only works around iSH's
# wait4 EINTR while compiling, so a native aarch64 build needs no wrapper; this is the
# same Alpine release as the guest, so the binary links the same musl, libwayland-server,
# libxkbcommon, zlib and libX11.
#
# Bind mounts are not shared into the colima VM, so the sources and the artifacts travel
# with docker cp.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
WS=$(cd "$HERE/../../.." && pwd)
SRC="$WS/work/LinPad/wl-bridge"
CTX=colima-linpad-fex
IMAGE=linpad-ishwl-builder:alpine3.21
DOCKER="docker --context $CTX"
DEPS="build-base wayland-dev wayland-protocols pkgconf zlib-dev libxkbcommon-dev libx11-dev"

# One reusable container with the build dependencies: apk add is the slow part.
if ! $DOCKER image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "preparing $IMAGE"
    id=$($DOCKER create alpine:3.21 sh -c 'while :; do sleep 3600; done')
    $DOCKER start "$id" >/dev/null
    $DOCKER exec "$id" apk add --no-cache $DEPS >/dev/null
    $DOCKER commit "$id" "$IMAGE" >/dev/null
    $DOCKER rm -f "$id" >/dev/null
fi

id=$($DOCKER create "$IMAGE" sh -c 'while :; do sleep 3600; done')
trap '$DOCKER rm -f "$id" >/dev/null' EXIT INT TERM
$DOCKER start "$id" >/dev/null
$DOCKER exec "$id" sh -c 'rm -rf /wb /out && mkdir -p /wb /out'
$DOCKER cp "$SRC/." "$id:/wb"
$DOCKER exec "$id" sh -c 'cd /wb && make -j4' 2>&1 | tail -20
$DOCKER exec "$id" sh -c 'cd /wb && cp -f ishwl libishwl-scm.so libishwl-compat.so ishwl-x11-wm /out/ 2>/dev/null || true'
$DOCKER cp "$id:/out/." "$HERE/"
$DOCKER exec "$id" sh -c 'readelf -d /out/ishwl | grep NEEDED'
echo "=== built into $HERE ==="
ls -l "$HERE/ishwl" "$HERE"/libishwl-*.so "$HERE"/ishwl-x11-wm 2>&1
