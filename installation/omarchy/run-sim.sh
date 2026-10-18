#!/bin/sh
# Installs the built simulator app on a booted simulator, launches it, and prints where the
# guest's logs are. The app's Linux system lives inside its own container, so everything the
# session writes (/tmp/ishwl.log, /run/omarchy-pad/*, /tmp/omarchy-pad/status) is a host file
# here:
#
#   <container>/Library/Application Support/roots/omarchy/data/...
#
#   sh installation/omarchy/run-sim.sh [udid] [--fresh]
#
# --fresh deletes the imported root first, so the app re-imports the image it carries (that is
# the only way to see a new rootfs; an install over an existing root keeps its Linux system).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$here/../.." && pwd)   # the LinPad checkout
WS=$(cd "$ROOT/../.." && pwd)     # the workspace that holds work/

udid=${1:-578191CA-B000-4D95-87A0-71873AC0B6E1}
fresh=0
for arg in "$@"; do
    if [ "$arg" = "--fresh" ]; then fresh=1; fi
done
bundle_id=com.valentinneagu.ish.arm64

app=$(cat "$ROOT/work/omarchy-pad/app/sim/last-app.txt" 2>/dev/null || true)
[ -n "$app" ] && [ -d "$app" ] || { echo "$0: no built app; run build-sim.sh first" >&2; exit 2; }

container=$(xcrun simctl get_app_container "$udid" "$bundle_id" data 2>/dev/null || true)
if [ "$fresh" = 1 ] && [ -n "$container" ]; then
    echo "removing the imported root so the app re-imports the image it carries"
    rm -rf "$container/Library/Application Support/roots/omarchy"
fi

xcrun simctl terminate "$udid" "$bundle_id" 2>/dev/null || true
xcrun simctl install "$udid" "$app"
echo "installed $(basename "$app") on $udid"
xcrun simctl launch "$udid" "$bundle_id" | sed 's/^/launched: /'

container=$(xcrun simctl get_app_container "$udid" "$bundle_id" data)
root="$container/Library/Application Support/roots/omarchy/data"
cat <<EOF
guest root: $root
logs:
  $root/tmp/ishwl.log                 ishwl itself
  $root/run/omarchy-pad/init.log      the supervisor: daemons and launch requests
  $root/run/omarchy-pad/launched.log  what a launched command printed
  $root/run/omarchy-pad/session.log   the session: Hyprland, the bridge
  $root/tmp/omarchy-pad/status        what the canvas is showing
  $root/tmp/omarchy-pad/error         set when the session gave up
EOF
