#!/bin/sh
# Builds and stages the Omarchy Pad variant: LinPad's one iSH-ARM64 target with
# LINPAD_VARIANT=omarchy, which boots the Omarchy root and shows the Omarchy canvas.
# Run it from the workspace root (the directory that holds work/, i.e. the parent of the
# LinPad checkout), like installation/steam-app/build-local.sh:
#
#   sh work/LinPad/installation/omarchy/build-local.sh
#
#   -> work/omarchy-pad/app/Payload/Omarchy Pad.app
#
# The variant is a build setting, not a second target: it lands in Info.plist (LinPadVariant)
# and decides what the app shows. Signing stays a separate, user-owned step (the app is built
# with CODE_SIGNING_ALLOWED=NO); installation/steam-app/sign-variant.py has the omarchy entry.
#
# ROOTFS_ARCHIVE is the Arch Linux ARM + Omarchy image. The default is the image the guest
# build in work/LinPad produces; ROOTFS_ARCHIVE=... overrides it. It is only used by a fresh
# container: an install over an existing app keeps its Linux system.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
variant=${1:-omarchy}
case $variant in
    omarchy) ;;
    *) echo "usage: $0 [omarchy]" >&2; exit 2 ;;
esac

# cd to the workspace root: the checkout is at work/LinPad, this script at
# work/LinPad/installation/omarchy, so the workspace root is four levels up.
cd "$here/../../../.."
root=$PWD
[ -f "$root/work/LinPad/iSH.xcodeproj/project.pbxproj" ] || {
    echo "$0: run it from the workspace root that holds work/ (looked in $root)" >&2; exit 2; }

# The guest image lives in the checkout's own work directory. Also accept a copy at the
# workspace-level work/omarchy-pad (where the gate evidence and staged app live).
default_rootfs=$root/work/LinPad/work/omarchy-pad/omarchy-aarch64-pad.tar.gz
if [ ! -f "$default_rootfs" ] && [ -f "$root/work/omarchy-pad/omarchy-aarch64-pad.tar.gz" ]; then
    default_rootfs=$root/work/omarchy-pad/omarchy-aarch64-pad.tar.gz
fi
ROOTFS_ARCHIVE=${ROOTFS_ARCHIVE:-$default_rootfs}
[ -f "$ROOTFS_ARCHIVE" ] || { echo "$0: no rootfs archive at $ROOTFS_ARCHIVE" >&2; exit 2; }

display='Omarchy Pad'
bundle_id='com.konradkern.omarchy'
# Build products stay in the checkout's work tree (the same paths the gate's xcodebuild
# command uses); the staged Payload lives in the workspace's work/omarchy-pad, next to the
# evidence.
builddir=$root/work/LinPad/work/omarchy-pad/app
outdir=$root/work/omarchy-pad/app
appname='Omarchy Pad.app'

# LinPad releases are numbered by the commit count (release/publish.sh:147); the version is
# the one in app/AppARM64.xcconfig. Passed explicitly so the staged bundle is right without
# patching it afterwards.
build_number=$(git -C "$root/work/LinPad" rev-list --count HEAD)
version=$(sed -n 's/^MARKETING_VERSION = //p' "$root/work/LinPad/app/AppARM64.xcconfig" | tr -d '[:space:]')
[ -n "$version" ] || { echo "$0: no MARKETING_VERSION in app/AppARM64.xcconfig" >&2; exit 2; }

mkdir -p "$outdir" "$builddir"
log=$outdir/build.log
echo "building $variant: LinPad $version build $build_number ($display)"

export DEVELOPER_DIR=${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}
export ZIG_GLOBAL_CACHE_DIR="$root/work/zig-cache"
PATH="$root/work/compiler-bin:$root/work/build-tools/bin:/opt/homebrew/bin:$PATH"
export PATH

"$DEVELOPER_DIR/usr/bin/xcodebuild" \
    -project "$root/work/LinPad/iSH.xcodeproj" -target iSH-ARM64 -configuration Release \
    -sdk iphoneos SYMROOT="$builddir/ios-build" OBJROOT="$builddir/ios-obj" \
    IPHONEOS_DEPLOYMENT_TARGET=17.0 ISH_JIT_BUILD=enabled CODE_SIGNING_ALLOWED=NO \
    MARKETING_VERSION="$version" CURRENT_PROJECT_VERSION="$build_number" \
    APP_DISPLAY_NAME="$display" LINPAD_VARIANT="$variant" \
    PRODUCT_BUNDLE_IDENTIFIER="$bundle_id" \
    PRODUCT_APP_GROUP_IDENTIFIER='group.com.konradkern.linpad' \
    ROOTFS_ARCHIVE="$ROOTFS_ARCHIVE" \
    build > "$log" 2>&1 || { echo "$0: xcodebuild failed, see $log" >&2; tail -20 "$log" >&2; exit 1; }

built="$builddir/ios-build/Release-iphoneos/$appname"
[ -d "$built" ] || built="$builddir/ios-build/Release-iphoneos/iSH ARM64.app"
[ -d "$built" ] || { echo "$0: no built app under $builddir/ios-build/Release-iphoneos" >&2; exit 1; }

# Stage it the way a signed release is staged: Payload/<name>.app, then sign in place.
rm -rf "$outdir/Payload"
mkdir -p "$outdir/Payload"
ditto "$built" "$outdir/Payload/$appname"

staged=$outdir/Payload/$appname
printf '%s\n' "staged $staged"
printf '%s\n' "variant=$variant version=$version build=$build_number display=$display"
# The executable keeps the target's PRODUCT_NAME ("iSH ARM64"); the .app's name is branding.
executable=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$staged/Info.plist" 2>/dev/null || echo "${appname%.app}")
printf '%s\n' "executable: $executable $(shasum -a 256 "$staged/$executable" | cut -d' ' -f1)"
# What the build actually baked in, read back from the staged bundle.
printf '%s\n' "LinPadVariant=$(/usr/libexec/PlistBuddy -c 'Print :LinPadVariant' "$staged/Info.plist")"
printf '%s\n' "CFBundleIdentifier=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$staged/Info.plist")"

# Facts the signer and the manifest need later.
{
    printf 'variant=%s\n' "$variant"
    printf 'version=%s\n' "$version"
    printf 'build=%s\n' "$build_number"
    printf 'display_name=%s\n' "$display"
    printf 'bundle_id=%s\n' "$bundle_id"
    printf 'source_commit=%s\n' "$(git -C "$root/work/LinPad" rev-parse HEAD)"
    printf 'rootfs_archive=%s\n' "$ROOTFS_ARCHIVE"
    printf 'rootfs_version=%s\n' "$(cat "$(dirname "$ROOTFS_ARCHIVE")/omarchy-aarch64-pad.version" 2>/dev/null || echo unknown)"
} > "$outdir/build-facts.txt"
cat "$outdir/build-facts.txt"
