#!/bin/sh
# Checks the iPad-keyboard chain inside the running app guest by typing into a terminal on the
# Omarchy desktop and reading the canvas back:
#
#   app channel -> ishwl seat -> bridge -> Hyprland zwp_virtual_keyboard_v1 -> client -> frame
#
#   sh installation/omarchy/probe-keyboard.sh [simulator-udid]
#
# It starts a terminal in the guest, focuses the canvas view over ishwl's own events channel
# (the message the app sends; the view id is read from ishwl's log because every session
# numbers the surface again), releases any modifier a previous run left pressed, types "hello"
# as raw evdev codes, and OCRs a screenshot of the canvas. "hello" appearing there -- in the
# terminal's own echo -- means the whole guest side of the keyboard works.
#
# Why not a file the client writes: a client that runs `read line` in this guest returns at
# once with an empty line, because the emulated pty answers EOF before any input arrives. A
# file-based check therefore reports "GOT=[]" whether or not the keys work; the canvas does not
# lie. (Evidence: work/omarchy-pad/evidence/gate-0.7/keyboard-chain.txt.)
set -eu

UDID=${1:-${OMARCHY_SIM_UDID:-FE9E27CC-3505-488F-A17F-89E4BC90E5D6}}
HERE=$(cd "$(dirname "$0")" && pwd)
DATA=$(xcrun simctl get_app_container "$UDID" com.valentinneagu.ish.arm64 data)/"Library/Application Support/roots/omarchy/data"
EVENTS=$DATA/tmp/ishwl/events
STATUS=$DATA/tmp/omarchy-pad/status
OCR=${OCR:-$HOME/Library/Caches/dsh-ios/bin/ocr}

[ -p "$EVENTS" ] || { echo "$0: no events FIFO at $EVENTS (is the app running?)" >&2; exit 2; }

echo "== bridge before"
grep -E '^input_(key|keymaps)=' "$STATUS" || true

echo "== starting a terminal in the guest"
OMARCHY_EVENTS="$EVENTS" python3 "$HERE/send-guest-cmd.py" \
    'pkill -x foot; sleep 2; WAYLAND_DISPLAY=wayland-1 foot -T keyprobe -e sh -c "printf KEYPROBE-READY; sleep 120" &' >/dev/null
sleep 28

VIEW=$(grep -o "map toplevel [0-9]* 'Omarchy' omarchy-pad" "$DATA/tmp/ishwl.log" 2>/dev/null | tail -1 | awk '{print $3}')
[ -n "${VIEW:-}" ] || { echo "$0: no omarchy-pad toplevel in ishwl.log (is the session up?)" >&2; exit 1; }
echo "== focusing the canvas view $VIEW and typing hello"
EVENTS=$EVENTS VIEW=$VIEW python3 - <<'PY'
import os, time
events, view = os.environ["EVENTS"], os.environ["VIEW"]
# Release the modifiers a previous code sweep may have left pressed, or the letters come out
# as capitals (which is how this was noticed the first time).
stale = [42, 54, 58, 29, 97, 125]
with open(events, "w") as f:
    f.write(f"focus {view}\n"); f.flush(); time.sleep(2)
    for code in stale:
        f.write(f"key {code} 0\n"); f.flush(); time.sleep(0.05)
    time.sleep(1)
    for code in (35, 18, 38, 38, 24):        # h e l l o
        f.write(f"key {code} 1\n"); f.flush(); time.sleep(0.25)
        f.write(f"key {code} 0\n"); f.flush(); time.sleep(0.25)
PY
sleep 8

echo "== bridge after"
grep -E '^input_(key|keymaps)=' "$STATUS" || true

shot=$(mktemp -t keyprobe).png
xcrun simctl io "$UDID" screenshot "$shot" >/dev/null 2>&1
text=$("$OCR" "$shot" 2>/dev/null || true)
echo "== canvas says: $text"

# The letters have to arrive in order, not letter for letter: key repeat and whatever
# modifier state a previous run left in the compositor can add characters between them
# ("heLLLLLLLo" is this chain working). Case is ignored for the same reason.
if TEXT="$text" python3 -c '
import os, sys
text = os.environ["TEXT"].lower()
it = iter(text)
print("KEYBOARD-CHAIN-OK" if all(ch in it for ch in "hello") else "KEYBOARD-CHAIN-BROKEN")
sys.exit(0 if all(ch in iter(text) for ch in "hello") else 1)
'; then
    rm -f "$shot"
else
    echo "no 'hello' in order on the canvas; screenshot kept at $shot"
    exit 1
fi
