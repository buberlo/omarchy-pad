#!/bin/sh
# Builds aquamarine-pad and hyprland-pad for Omarchy Pad gate 0.4.
#
#   sh build.sh [aquamarine|hyprland|all]
#
# The packages are built in an aarch64 Arch Linux container (native speed on Apple
# Silicon, unlike the emulator) and copied to $OUT (default
# <repo>/work/omarchy-pad/gate-0.4/pkgs). makepkg is run as an unprivileged user;
# pacman's sandbox has to be disabled inside the container (Docker's seccomp profile
# blocks Landlock).
#
# Env:
#   CONTAINER  docker container (default gate04-build; created if missing)
#   CONTEXT    docker context (default colima-linpad-fex)
#   OUT        output directory for the .pkg.tar.zst files
#   GUEST_ARCH container image (default menci/archlinuxarm:latest)
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
# <repo> = the space-free tree that holds work/LinPad and work/omarchy-pad
ROOT=$(cd "$HERE/../../../../.." && pwd)
SCRATCH=${SCRATCH:-$ROOT/work/omarchy-pad/gate-0.4}
OUT=${OUT:-$SCRATCH/pkgs}
CONTAINER=${CONTAINER:-gate04-build}
CONTEXT=${CONTEXT:-colima-linpad-fex}
IMAGE=${GUEST_ARCH:-menci/archlinuxarm:latest}
WHAT=${1:-all}

DOCKER="docker --context $CONTEXT"
SRC=$SCRATCH/src
STAGE=$SCRATCH/build-stage

################################################################################
# 1. stage: sources, PKGBUILDs (named PKGBUILD for makepkg) and patches
################################################################################
need() { [ -e "$1" ] || { echo "build.sh: missing $1" >&2; exit 1; }; }

need "$SRC/hyprland-source-v0.56.2.tar.gz"
need "$SRC/aquamarine-0.15.1.tar.gz"
need "$HERE/PKGBUILD.aquamarine-pad"
need "$HERE/PKGBUILD.hyprland-pad"

rm -rf "$STAGE"
mkdir -p "$STAGE/aquamarine-pad" "$STAGE/hyprland-pad" "$OUT"

cp "$SRC/aquamarine-0.15.1.tar.gz"                      "$STAGE/aquamarine-pad/"
cp "$HERE/patches/aquamarine/"*.patch                   "$STAGE/aquamarine-pad/"
cp "$HERE/PKGBUILD.aquamarine-pad"                      "$STAGE/aquamarine-pad/PKGBUILD"

cp "$SRC/hyprland-source-v0.56.2.tar.gz"                "$STAGE/hyprland-pad/"
cp "$HERE/patches/hyprland/"*.patch                     "$STAGE/hyprland-pad/"
cp "$HERE/patches/hyprland/omarchy/"*.patch             "$STAGE/hyprland-pad/"
cp "$HERE/PKGBUILD.hyprland-pad"                        "$STAGE/hyprland-pad/PKGBUILD"

################################################################################
# 2. container
################################################################################
if ! $DOCKER inspect "$CONTAINER" >/dev/null 2>&1; then
    echo "build.sh: creating container $CONTAINER"
    $DOCKER create --name "$CONTAINER" -w / "$IMAGE" sh -c 'while :; do sleep 3600; done' >/dev/null
fi
$DOCKER start "$CONTAINER" >/dev/null 2>&1 || true

echo "build.sh: syncing sources into $CONTAINER:/build/gate04"
$DOCKER exec "$CONTAINER" rm -rf /build/gate04/src
$DOCKER exec "$CONTAINER" mkdir -p /build/gate04/src
COPYFILE_DISABLE=1 tar --no-xattrs --no-mac-metadata -C "$STAGE" -cf - . | $DOCKER exec -i "$CONTAINER" tar -C /build/gate04/src -xf -

################################################################################
# 3. build inside the container
################################################################################
$DOCKER exec -i -e GATE04_WHAT="$WHAT" "$CONTAINER" sh -s <<'CONTAINER_SCRIPT'
set -eu
WHAT=${GATE04_WHAT:-all}
BUILD=/build/gate04

# makepkg must not run as root; pacman needs its sandbox off under Docker's seccomp
id builder >/dev/null 2>&1 || useradd -m -s /bin/sh builder
grep -q '^DisableSandbox' /etc/pacman.conf || sed -i '/^\[options\]/a DisableSandbox' /etc/pacman.conf

DEPS="base-devel cmake meson ninja git zstd glibc libgcc libstdc++ glaze hyprwayland-scanner
      hyprland-protocols xorgproto hyprutils hyprlang hyprcursor hyprgraphics hyprwire
      hyprland-guiutils aquamarine libdisplay-info seatd lua muparser tomlplusplus re2 pixman
      libdrm libglvnd mesa wayland wayland-protocols libxkbcommon libinput cairo pango glib2
      glslang lcms2 libxcb libx11 libxcomposite libxcursor libxfixes libxrender xorg-xwayland
      xcb-util xcb-util-errors xcb-util-image xcb-util-keysyms xcb-util-renderutil xcb-util-wm
      xcb-proto util-linux-libs opengl-driver grim"

echo "=== installing build dependencies ==="
pacman -S --noconfirm --needed --disable-sandbox $DEPS >"$BUILD/deps.log" 2>&1 || {
    echo "pacman failed, tail of $BUILD/deps.log:"; tail -30 "$BUILD/deps.log"; exit 1; }

build_pkg() {
    NAME=$1
    echo "=== makepkg $NAME ==="
    chown -R builder "$BUILD/src"
    su builder -c "cd $BUILD/src/$NAME && PKGEXT='.pkg.tar.zst' makepkg -A --nodeps --noconfirm --noprogressbar" || return 1
    find "$BUILD/src/$NAME" -maxdepth 1 -name '*.pkg.tar.*' -exec cp -v {} "$BUILD/pkgs/" \;
}

mkdir -p "$BUILD/pkgs"
case "$WHAT" in
    aquamarine) build_pkg aquamarine-pad ;;
    hyprland)   build_pkg hyprland-pad ;;
    all)        build_pkg aquamarine-pad && build_pkg hyprland-pad ;;
    *)          echo "unknown target $WHAT" >&2; exit 2 ;;
esac
echo "=== packages ==="
ls -la "$BUILD/pkgs"
CONTAINER_SCRIPT

################################################################################
# 4. copy artifacts back
################################################################################
echo "build.sh: copying packages to $OUT"
$DOCKER exec "$CONTAINER" sh -c 'ls /build/gate04/pkgs/*.pkg.tar.zst 2>/dev/null' | while read -r f; do
    $DOCKER cp "$CONTAINER:$f" "$OUT/"
done
ls -la "$OUT"/*.pkg.tar.zst
