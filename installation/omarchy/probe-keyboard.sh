#!/bin/sh
# Checks the iPad-keyboard chain inside the running app guest, one end at a time:
#
#   app -> ishwl seat -> bridge -> Hyprland virtual keyboard -> focused client
#
#   sh installation/omarchy/probe-keyboard.sh [simulator-udid]
#
# It starts a terminal in the guest that puts its pty in raw mode and writes what it receives
# to /tmp/keydump.txt, focuses the canvas view over ishwl's own events channel (the same
# message the app sends), types "hello" plus Enter as raw evdev codes through that channel,
# and prints the dump. A dump of 68 65 6c 6c 6f 0d ("hello\r") means the whole chain works.
#
# State as of the round this was written: everything up to and including the bridge works
# (the bridge reports the keys it forwards, and Hyprland lists the virtual keyboard device
# with the keymap parsed), but no key reaches a client or fires a compositor bind. The script
# is how that was established and is what a fix has to make pass.
set -eu

UDID=${1:-${OMARCHY_SIM_UDID:-FE9E27CC-3505-488F-A17F-89E4BC90E5D6}}
HERE=$(cd "$(dirname "$0")" && pwd)
DATA=$(xcrun simctl get_app_container "$UDID" com.valentinneagu.ish.arm64 data)/"Library/Application Support/roots/omarchy/data"
EVENTS=$DATA/tmp/ishwl/events
STATUS=$DATA/tmp/omarchy-pad/status

[ -p "$EVENTS" ] || { echo "$0: no events FIFO at $EVENTS (is the app running?)" >&2; exit 2; }

echo "== bridge before"
grep -E '^input_(key|keymaps)=' "$STATUS" || true

echo "== starting a raw-mode client in the guest"
OMARCHY_EVENTS="$EVENTS" python3 "$HERE/send-guest-cmd.py" \
    'pkill -x foot; sleep 2; rm -f /tmp/keydump.txt /tmp/keydump-up.txt; WAYLAND_DISPLAY=wayland-1 foot -T keyprobe -e sh -c "echo UP > /tmp/keydump-up.txt; stty raw -echo; timeout 70 cat > /tmp/keydump.txt" &' >/dev/null
sleep 28
[ -f "$DATA/tmp/keydump-up.txt" ] || { echo "$0: the client did not start" >&2; exit 1; }

# The canvas surface has a new view id in every session (ishwl numbers them in order), and
# ishwl only delivers keys to the focused view: take the id from the compositor's own log.
VIEW=$(grep -o "map toplevel [0-9]* 'Omarchy' omarchy-pad" "$DATA/tmp/ishwl.log" 2>/dev/null | tail -1 | awk '{print $3}')
[ -n "${VIEW:-}" ] || { echo "$0: no omarchy-pad toplevel in ishwl.log (is the session up?)" >&2; exit 1; }
echo "== focusing the canvas view $VIEW, then typing hello<Enter>"
printf 'focus %s\n' "$VIEW" > "$EVENTS"
python3 - "$EVENTS" <<'PY'
import sys, time
with open(sys.argv[1], 'w') as f:
    for code in (35, 18, 38, 38, 24, 28):          # h e l l o <Enter>
        f.write(f"key {code} 1\n"); f.flush(); time.sleep(0.18)
        f.write(f"key {code} 0\n"); f.flush(); time.sleep(0.18)
PY
sleep 12

echo "== bridge after"
grep -E '^input_(key|keymaps)=' "$STATUS" || true
echo "== what the client received"
python3 - "$DATA/tmp/keydump.txt" <<'PY'
import binascii, os, sys
p = sys.argv[1]
data = open(p, 'rb').read() if os.path.exists(p) else b''
print(f"{len(data)} bytes: {binascii.hexlify(data).decode()} {data!r}")
if data == b"hello\r" or data == b"hello\n":
    print("KEYBOARD-CHAIN-OK")
else:
    print("KEYBOARD-CHAIN-BROKEN (nothing reached the client)")
PY
