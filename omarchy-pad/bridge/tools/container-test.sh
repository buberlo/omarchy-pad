#!/bin/sh
# Bridge + ishwl + mock compositor, all natively inside the aarch64 container: the fast
# loop for the frame pipeline, the input injection values and the configure -> hyprctl ->
# output mode -> buffer size chain.  Hyprland itself cannot run here (see README.md); the
# mock implements the four globals the bridge binds and a stub hyprctl stands in for the
# real one, so the bridge runs unmodified.
#
#   P=/pad sh /pad/tools/container-test.sh
set -u
P=${P:-/pad}
R=/tmp/omarchy-pad
export XDG_RUNTIME_DIR=/tmp/xdg
export OMARCHY_PAD_STUB_LOG=$R/hyprctl-stub.log
export OMARCHY_PAD_STUB_MODE=$R/mock-mode

# No killall in a bare container: kill by /proc name.
for n in mock-compositor omarchy-pad-bridge ishwl; do
    for p in /proc/[0-9]*; do
        [ -r "$p/comm" ] || continue
        [ "$(cat "$p/comm" 2>/dev/null)" = "$n" ] && kill -9 "${p#/proc/}" 2>/dev/null
    done
done
sleep 1
rm -rf "$R" /tmp/ishwl "$XDG_RUNTIME_DIR"
mkdir -p "$XDG_RUNTIME_DIR" "$R/bin"
chmod 700 "$XDG_RUNTIME_DIR"
E=$R/evidence
mkdir -p "$E"

# A hyprctl that records the command and asks the mock to change the output mode.
cat >"$R/bin/hyprctl" <<'STUB'
#!/bin/sh
echo "$(date +%s) hyprctl $*" >>"$OMARCHY_PAD_STUB_LOG"
case "$*" in
    *keyword*monitor*)
        size=$(printf '%s' "$*" | sed -n 's/.*, *\([0-9][0-9]*x[0-9][0-9]*\)@.*/\1/p')
        [ -n "$size" ] && printf '%s\n' "$size" >"$OMARCHY_PAD_STUB_MODE"
        ;;
esac
echo "ok"
STUB
chmod 755 "$R/bin/hyprctl"

"$P/mock-compositor" --socket wayland-1 --instance-dir hypr/mock --runtime-dir "$R" >"$E/mock.out" 2>&1 &
sleep 1
"$P/ishwl" --headless -s wayland-0 -r /tmp/ishwl -g 1280x800 -S 1 -v >"$E/ishwl.out" 2>&1 &
sleep 1
PATH="$R/bin:$PATH" WAYLAND_DISPLAY=wayland-0 "$P/omarchy-pad-bridge" -v >"$E/bridge.out" 2>&1 &
sleep 4

echo "=== status after start"
cat "$R/status"
echo "=== first frames"
head -3 "$R/frames.log" 2>/dev/null
echo "=== mock.log"
grep -E "bound|created|copied" "$R/mock.log" | head -8
echo "=== ishwl view buffers"
ls -la /tmp/ishwl/v*.buf 2>/dev/null

echo "=== input"
echo "motion 1 100 200" >/tmp/ishwl/events
sleep 1
echo "motion 1 700 500" >/tmp/ishwl/events
sleep 1
echo "button 1 700 500 272 1" >/tmp/ishwl/events
sleep 1
echo "button 1 700 500 272 0" >/tmp/ishwl/events
sleep 1
echo "axis 1 0 -30 1" >/tmp/ishwl/events
sleep 1
echo "axis_stop 1" >/tmp/ishwl/events
sleep 1
echo "focus 1" >/tmp/ishwl/events
sleep 1
echo "key 63 1" >/tmp/ishwl/events
sleep 1
echo "key 63 0" >/tmp/ishwl/events
sleep 2
echo "=== mock status"
cat "$R/mock-status"
echo "=== what the compositor side saw"
grep -E "input |mode change" "$R/mock.log"

echo "=== resize: configure 1024x768 through the bridge's own hyprctl"
echo "configure 1 1024 768 0" >/tmp/ishwl/events
sleep 4
echo "--- hyprctl-stub.log"
cat "$R/hyprctl-stub.log" 2>/dev/null
echo "--- status"
cat "$R/status"
echo "--- last frames"
tail -3 "$R/frames.log"
echo "--- mock status"
cat "$R/mock-status"
echo "--- bridge errors"
grep -iE "error|warning|cannot" "$E/bridge.out" | head -5

# Copy the ishwl view buffer before anything dies: ishwl unlinks it when the client goes.
for f in /tmp/ishwl/v*.buf; do
    [ -f "$f" ] && cp "$f" "$E/$(basename "$f")" && echo "copied $f ($(wc -c <"$f") bytes)"
done

for n in mock-compositor omarchy-pad-bridge ishwl; do
    for p in /proc/[0-9]*; do
        [ -r "$p/comm" ] || continue
        [ "$(cat "$p/comm" 2>/dev/null)" = "$n" ] && kill -9 "${p#/proc/}" 2>/dev/null
    done
done
echo "=== done"
