#!/bin/sh
# Measures the input queue wait in src/textinput.c with a headless ishwl and the
# textinput-order-test client, and prints what the client saw for each committed string:
#
#   sh tools/textinput-order-test.sh [path-to-ishwl] [label]
#
# Scenario, written into the bridge's events FIFO as the host would send it:
#   1. a bare key press (W, evdev 17) followed by committed text
#   2. an edit key (Backspace, evdev 14) followed by committed text
#   3. two committed strings in a row, no key between them
#
# Expected with the current source: the text after the bare key arrives immediately (a bare key
# event is followed by nothing the app could acknowledge), the text after Backspace waits for
# ORDER_TIMEOUT_MS because that key edits text, and the two strings in step 3 are back to back.
# Run it with an older ishwl to see the behaviour it replaced: there the text after the bare key
# waits ~250 ms too, because every key press held the queue.
#
# Needs wayland-scanner (wayland-dev, wayland-protocols, pkgconf) and a built ishwl.
set -u
cd "$(dirname "$0")/.."
ishwl=${1:-./ishwl}
label=${2:-$(basename "$ishwl")}
test -x "$ishwl" || { echo "not executable: $ishwl" >&2; exit 66; }

B=build/textinput-order-test
mkdir -p "$B"
XDG=$(pkg-config --variable=pkgdatadir wayland-protocols)/stable/xdg-shell/xdg-shell.xml
TI=$(pkg-config --variable=pkgdatadir wayland-protocols)/unstable/text-input/text-input-unstable-v3.xml
wayland-scanner client-header "$XDG" "$B/xdg-shell-client.h"
wayland-scanner private-code "$XDG" "$B/xdg-shell.c"
wayland-scanner client-header "$TI" "$B/text-input-unstable-v3-client.h"
wayland-scanner private-code "$TI" "$B/text-input-unstable-v3.c"
${CC:-cc} -o "$B/client" tools/textinput-order-test.c "$B/xdg-shell.c" "$B/text-input-unstable-v3.c" \
    -I"$B" $(pkg-config --cflags --libs wayland-client) || exit 1

RT=/tmp/keyorder-rt-$label
SOCK=wl-keyorder-$label
export XDG_RUNTIME_DIR=/tmp/keyorder-xdg-$label
rm -rf "$RT" "$XDG_RUNTIME_DIR"
mkdir -p "$XDG_RUNTIME_DIR"
"$ishwl" -H -r "$RT" -s "$SOCK" -v >"$B/ishwl-$label.log" 2>&1 &
ishwl_pid=$!
for _ in $(seq 50); do [ -p "$RT/events" ] && break; sleep 0.2; done
[ -p "$RT/events" ] || { echo "ishwl did not start (see $B/ishwl-$label.log)" >&2; kill "$ishwl_pid" 2>/dev/null; exit 69; }

WAYLAND_DISPLAY=$SOCK "$B/client" 12 >"$B/client-$label.log" 2>&1 &
client_pid=$!
for _ in $(seq 50); do grep -q "^ready " "$B/client-$label.log" && break; sleep 0.2; done
sleep 0.5

send() { printf '%s\n' "$@" > "$RT/events"; }
send "focus 1"
sleep 0.5
send "keymap de mac -"
sleep 0.5

# 1: bare key, then text
send "key 17 1" "key 17 0" "ime 0 0 A - 0 0"
sleep 1.2
# 2: edit key, then text
send "key 14 1" "key 14 0" "ime 0 0 B - 0 0"
sleep 1.2
# 3: two strings, no key between them
send "ime 0 0 C - 0 0"
sleep 0.3
send "ime 0 0 D - 0 0"
sleep 0.6

kill "$client_pid" 2>/dev/null
kill "$ishwl_pid" 2>/dev/null
wait "$client_pid" 2>/dev/null
wait "$ishwl_pid" 2>/dev/null

echo "=== $label: client log ($B/client-$label.log) ==="
cat "$B/client-$label.log"
echo
echo "=== $label: what followed what ==="
awk '
/^KEY [0-9]+ down/ { keycode = $2; keytime = $4 }
/^TEXT /          { if (keycode != "") {
                        printf "TEXT %s after KEY %s down: %.1f ms\n", $2, keycode, $3 - keytime
                    } else {
                        printf "TEXT %s with no key before it: %.1f ms\n", $2, $3
                    }
                    keycode = "" }
' "$B/client-$label.log"
