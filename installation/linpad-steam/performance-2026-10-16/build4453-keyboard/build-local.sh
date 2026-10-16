#!/bin/sh
# Build 4453: the keyboard fix (b432013e) in DesktopKit. Same app target and rootfs as
# build 4452; the Linux system is not rebuilt and the bundled root.tar.gz is the installed
# one (root.version 202610031701), so installing this over 4452 keeps the Linux system,
# Steam, the game and both save slots.
#
# Run from the workspace root (the directory holding work/). The Zig cache and the
# compiler wrapper are the ones recorded in installation/linpad-steam/README.md; build
# 4452's copy of this script omits them and was not the command used for this build.
set -eu
cd "$(dirname "$0")/../../../../.."
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
ZIG_GLOBAL_CACHE_DIR="$PWD/work/zig-cache"
PATH="$PWD/work/compiler-bin:$PWD/work/build-tools/bin:/opt/homebrew/bin:$PATH"
export ZIG_GLOBAL_CACHE_DIR PATH
xcodebuild -project work/LinPad/iSH.xcodeproj -target iSH-ARM64 -configuration Release \
    -sdk iphoneos SYMROOT="$PWD/work/ios-build" OBJROOT="$PWD/work/ios-obj" \
    IPHONEOS_DEPLOYMENT_TARGET=17.0 ISH_JIT_BUILD=enabled CODE_SIGNING_ALLOWED=NO \
    ROOTFS_ARCHIVE="$PWD/work/signed/Payload/iSH ARM64.app/root.tar.gz" \
    build > "$PWD/work/performance/build4453-build.log" 2>&1
echo "built: $(shasum -a 256 "$PWD/work/ios-build/Release-iphoneos/iSH ARM64.app/iSH ARM64")"
