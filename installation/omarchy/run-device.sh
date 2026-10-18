#!/bin/sh
# Builds nothing: installs the already-signed device build of the Omarchy Pad variant on a
# connected iPad, launches it, and follows what the app reports while it unpacks the image and
# boots Linux.
#
#   sh installation/omarchy/run-device.sh [udid]
#
# The device build is made by installation/omarchy/... (see docs/omarchy/GATES.md, Phase 1):
# xcodebuild for iphoneos with Xcode-managed signing, which is the only way to get a profile
# for a bundle id that does not exist yet.
#
# Note the app's own logs are the only window on the device guest: the container is not
# readable from the Mac, so the boot progress in the system log plus a screenshot of the iPad
# are the evidence.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
WS=$(cd "$ROOT/../.." && pwd)
UDID=${1:-$(idevice_id -l 2>/dev/null | head -1)}
[ -n "${UDID:-}" ] || { echo "$0: no device connected (idevice_id -l is empty)" >&2; exit 2; }

APP=$(ls -d "$ROOT"/work/omarchy-pad/app/device/products/Release-iphoneos/*.app 2>/dev/null | head -1)
[ -n "${APP:-}" ] || { echo "$0: no device build; see the Phase 1 notes in docs/omarchy/GATES.md" >&2; exit 2; }
BUNDLE=com.konradkern.omarchy
LOG=${LOG:-$WS/work/omarchy-pad/device-syslog.txt}

echo "device $UDID"
echo "app    $APP"
timeout 900 xcrun devicectl device install app --device "$UDID" "$APP" | tail -3

pkill -f "idevicesyslog -u $UDID" 2>/dev/null || true
: > "$LOG"
( idevicesyslog -u "$UDID" -o "$LOG" >/dev/null 2>&1 & ) || true
sleep 2

echo "launching $BUNDLE (screen must be awake and unlocked, or iOS suspends it)"
timeout 120 xcrun devicectl device process launch --device "$UDID" --terminate-existing "$BUNDLE" | tail -2

echo "--- following the app's progress (Ctrl-C stops watching; the app keeps running)"
tail -f "$LOG" 2>/dev/null | grep --line-buffered -aE "\[boot\]|\[import\]|Unpack Linux|ishwl|omarchy|guest" | head -80
