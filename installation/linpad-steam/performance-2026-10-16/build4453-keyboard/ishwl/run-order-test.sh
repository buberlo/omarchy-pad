#!/bin/sh
# Runs wl-bridge's tools/textinput-order-test.sh in the aarch64 colima VM against the current
# wl-bridge and against the revision the device ran before this change (a6e0672e), and copies
# both logs next to this script. The comparison is the point: the same scenario, two binaries.
#
# Needs: the colima profile "linpad-fex" running and the image linpad-ishwl-builder:alpine3.21
# (work/performance/build4453-keyboard/build-ishwl-aarch64.sh creates it).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
WS=$(cd "$HERE/../../../../../../.." && pwd)
REPO="$WS/work/LinPad"
REF=${REF:-a6e0672e}
DOCKER="docker --context colima-linpad-fex"

rm -rf /tmp/wl-ref && mkdir -p /tmp/wl-ref
(cd "$REPO" && git archive "$REF" wl-bridge) | tar -x -C /tmp/wl-ref

id=$($DOCKER create linpad-ishwl-builder:alpine3.21 sh -c 'sleep 1200')
trap '$DOCKER rm -f "$id" >/dev/null 2>&1 || true' EXIT INT TERM
$DOCKER start "$id" >/dev/null
$DOCKER exec "$id" sh -c 'apk add --no-cache xkeyboard-config >/dev/null 2>&1; mkdir -p /new /old'
$DOCKER cp "$REPO/wl-bridge/." "$id:/new"
$DOCKER cp /tmp/wl-ref/wl-bridge/. "$id:/old"

$DOCKER exec "$id" sh -c '
set -e
cd /new && make -j4 >/dev/null 2>&1 && echo "patched ishwl built"
cd /old && make -j4 >/dev/null 2>&1 && echo "reference ishwl built"
cd /new && sh tools/textinput-order-test.sh ./ishwl patched > /tmp/out-patched.txt 2>&1 || echo "patched run exit $?"
cd /new && sh tools/textinput-order-test.sh /old/ishwl reference > /tmp/out-reference.txt 2>&1 || echo "reference run exit $?"
cat /tmp/out-patched.txt
echo "################"
cat /tmp/out-reference.txt
'
$DOCKER cp "$id:/tmp/out-patched.txt" "$HERE/order-test-patched.txt"
$DOCKER cp "$id:/tmp/out-reference.txt" "$HERE/order-test-reference.txt"
echo "=== copied to $HERE/order-test-{patched,reference}.txt ==="
