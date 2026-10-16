#!/bin/sh
set -eu
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
xcodebuild -project work/LinPad/iSH.xcodeproj -target iSH-ARM64 -configuration Release -sdk iphoneos SYMROOT="$(pwd)/work/ios-build" OBJROOT="$(pwd)/work/ios-obj" IPHONEOS_DEPLOYMENT_TARGET=17.0 ISH_JIT_BUILD=enabled CODE_SIGNING_ALLOWED=NO ROOTFS_ARCHIVE="$(pwd)/work/signed/Payload/iSH ARM64.app/root.tar.gz" build
