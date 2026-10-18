#!/bin/sh
# Builds the Omarchy Pad variant for the iOS Simulator, so the guest and the canvas can be
# exercised without a device:
#
#   sh work/LinPad/installation/omarchy/build-sim.sh
#   -> work/LinPad/work/omarchy-pad/app/sim-build/Release-iphonesimulator/Omarchy Pad.app
#
# Same variant switch as build-local.sh (LINPAD_VARIANT=omarchy), but for the simulator SDK
# and with the interpreter only: a simulator build cannot use the JIT (ISH_JIT_BUILD=disabled),
# which is also what an unsigned device install without a debugging entitlement gets.
#
# The rootfs is copied into the bundle by a build phase whose only output is
# Contents/root.tar.gz. Xcode skips that phase when the file is already there, so a bundle
# built before a guest rebuild keeps booting the OLD image -- the version is therefore read
# back out of the built bundle and printed (and a mismatch against the archive fails). The
# products directory is removed first for the same reason.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
cd "$here/../../../.."
root=$PWD
[ -f "$root/work/LinPad/iSH.xcodeproj/project.pbxproj" ] || {
    echo "$0: run it from the workspace root that holds work/ (looked in $root)" >&2; exit 2; }

default_rootfs=$root/work/LinPad/work/omarchy-pad/omarchy-aarch64-pad.tar.gz
if [ ! -f "$default_rootfs" ] && [ -f "$root/work/omarchy-pad/omarchy-aarch64-pad.tar.gz" ]; then
    default_rootfs=$root/work/omarchy-pad/omarchy-aarch64-pad.tar.gz
fi
ROOTFS_ARCHIVE=${ROOTFS_ARCHIVE:-$default_rootfs}
[ -f "$ROOTFS_ARCHIVE" ] || { echo "$0: no rootfs archive at $ROOTFS_ARCHIVE" >&2; exit 2; }

display='Omarchy Pad'
builddir=$root/work/LinPad/work/omarchy-pad/app/sim
symroot=$builddir/products
objroot=$builddir/obj
log=$builddir/build.log
mkdir -p "$builddir"
# The products directory must go: the rootfs copy phase is skipped when the bundle already
# has Contents/root.tar.gz, so an old bundle silently keeps booting the old image (the
# version is verified below). Finder browsing the build tree re-creates .DS_Store entries
# while rm walks it, which makes a single rm fail with "Directory not empty"; retry, and
# fall back to removing just the app.
if ! rm -rf "$symroot" 2>/dev/null; then
    echo "$0: could not clear $symroot entirely (something is recreating entries); removing the app" >&2
    rm -rf "$symroot/Release-iphonesimulator"
fi
find "$objroot" -name 'root.tar.gz' -delete 2>/dev/null || true

version=$(sed -n 's/^MARKETING_VERSION = //p' "$root/work/LinPad/app/AppARM64.xcconfig" | tr -d '[:space:]')
build_number=$(git -C "$root/work/LinPad" rev-list --count HEAD)
archive_version=$(cat "$(dirname "$ROOTFS_ARCHIVE")/omarchy-aarch64-pad.version" 2>/dev/null || echo unknown)
echo "building simulator app: LinPad $version build $build_number, rootfs $archive_version"

export DEVELOPER_DIR=${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}
export ZIG_GLOBAL_CACHE_DIR="$root/work/zig-cache"
PATH="$root/work/compiler-bin:$root/work/build-tools/bin:/opt/homebrew/bin:$PATH"
export PATH

"$DEVELOPER_DIR/usr/bin/xcodebuild" \
    -project "$root/work/LinPad/iSH.xcodeproj" -target iSH-ARM64 -configuration Release \
    -sdk iphonesimulator ARCHS=arm64 ONLY_ACTIVE_ARCH=NO \
    SYMROOT="$symroot" OBJROOT="$objroot" \
    IPHONEOS_DEPLOYMENT_TARGET=17.0 ISH_JIT_BUILD=disabled CODE_SIGNING_ALLOWED=NO \
    MARKETING_VERSION="$version" CURRENT_PROJECT_VERSION="$build_number" \
    APP_DISPLAY_NAME="$display" LINPAD_VARIANT=omarchy \
    PRODUCT_BUNDLE_IDENTIFIER=com.valentinneagu.ish.arm64 \
    ROOTFS_ARCHIVE="$ROOTFS_ARCHIVE" \
    build > "$log" 2>&1 || { echo "$0: xcodebuild failed, see $log" >&2; tail -20 "$log" >&2; exit 1; }

app=$symroot/Release-iphonesimulator/"$display.app"
[ -d "$app" ] || app=$symroot/Release-iphonesimulator/iSH\ ARM64.app
[ -d "$app" ] || { echo "$0: no app under $symroot/Release-iphonesimulator" >&2; exit 1; }

# What the bundle will really boot, read out of the archive inside it.
baked=$(tar -xzOf "$app/root.tar.gz" ./usr/share/ish/rootfs-version 2>/dev/null | tr -d '[:space:]')
[ -n "$baked" ] || { echo "$0: the bundle has no readable rootfs version" >&2; exit 1; }
[ "$baked" = "$archive_version" ] || {
    echo "$0: bundle booted $baked but the archive is $archive_version (stale copy phase)" >&2; exit 1; }

printf '%s\n' "built $app"
printf '%s\n' "rootfs_version=$baked (archive $archive_version)"
printf '%s\n' "LinPadVariant=$(/usr/libexec/PlistBuddy -c 'Print :LinPadVariant' "$app/Info.plist" 2>/dev/null || echo '?')"
printf '%s\n' "CFBundleIdentifier=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Info.plist" 2>/dev/null || echo '?')"
printf '%s\n' "$app" > "$builddir/last-app.txt"
