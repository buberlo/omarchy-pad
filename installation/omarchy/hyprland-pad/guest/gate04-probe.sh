#!/bin/sh
# Gate 0.4 probe, runs INSIDE the emulator:
#   ish -f fs-gate04 /bin/sh /tmp/gate04-probe.sh
#
# Starts the patched Hyprland on one headless output (created by the compositor itself
# from HYPR_PAD_HEADLESS_OUTPUT, because hyprctl cannot run under iSH), waits for the
# renderer, screencopies a frame with grim and leaves the logs, the PNG and the raw shm
# dump in /root/gate04-out (part of the fakefs, so they land on the host).

export XDG_RUNTIME_DIR=/run/user/0
export HOME=/root
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

# Renderer selection: patch 0001 makes the device platform work off the render node, so
# zink is tried first; MESA_LOADER_DRIVER_OVERRIDE= / LIBGL_ALWAYS_SOFTWARE=1 force llvmpipe.
export MESA_LOADER_DRIVER_OVERRIDE=${MESA_LOADER_DRIVER_OVERRIDE-zink}
export MESA_GLES_VERSION_OVERRIDE=${MESA_GLES_VERSION_OVERRIDE-3.0}
export MESA_VK_WSI_DEBUG=sw
export VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/virtio_icd.aarch64.json}
export WLR_RENDERER_ALLOW_SOFTWARE=1
export AQ_HEADLESS_DRM_NODE=${AQ_HEADLESS_DRM_NODE:-/dev/dri/renderD128}
# iSH's timerfd deadlocks (see the aquamarine patch); the patched aquamarine detects iSH
# itself, this is belt and braces.
export AQ_HEADLESS_NO_TIMERFD=${AQ_HEADLESS_NO_TIMERFD:-1}
# Hyprland does not create a headless output by itself and hyprctl cannot ask for one
# here (iSH does not implement setsockopt(SO_RCVTIMEO), so hyprctl aborts before it sends
# anything), so the compositor creates it from this variable (patch 0003).
export HYPR_PAD_HEADLESS_OUTPUT=${HYPR_PAD_HEADLESS_OUTPUT-HEADLESS-1}
export HYPRLAND_NO_CRASHREPORTER=1
export HYPRLAND_NO_SD_NOTIFY=1

OUT=/root/gate04-out
mkdir -p "$OUT"
export HYPR_PAD_SHM_DUMP=${HYPR_PAD_SHM_DUMP:-$OUT/frame.raw}
HYPRLAND=/usr/bin/hyprland

rm -f "$OUT"/frame.raw "$OUT"/shot.png "$OUT"/shot2.png "$OUT"/hypr.log "$OUT"/hyprland-instance.log
rm -rf "$XDG_RUNTIME_DIR"/hypr

echo "=== env ==="
echo "uname: $(uname -a)"
echo "hyprland: $HYPRLAND ($(stat -c %s $HYPRLAND 2>/dev/null) bytes)"
echo "grim: $(command -v grim)"
echo "renderer env: MESA_LOADER_DRIVER_OVERRIDE=${MESA_LOADER_DRIVER_OVERRIDE} MESA_GLES_VERSION_OVERRIDE=${MESA_GLES_VERSION_OVERRIDE} LIBGL_ALWAYS_SOFTWARE=${LIBGL_ALWAYS_SOFTWARE:-} AQ_HEADLESS_NO_TIMERFD=${AQ_HEADLESS_NO_TIMERFD} HYPR_PAD_HEADLESS_OUTPUT=${HYPR_PAD_HEADLESS_OUTPUT}"
ls -la /dev/dri/ 2>&1 | tail -2

cd /tmp
echo "=== starting hyprland ==="
# stdout redirected to a file is block buffered by stdio (4 KiB), so run it under a pty
# via script(1); debug:disable_logs = false in the config keeps the log complete.
if command -v script >/dev/null 2>&1; then
    script -q -c "$HYPRLAND --i-am-really-stupid -c /tmp/pad-hyprland.conf" /dev/null > "$OUT/hypr.log" 2>&1 &
else
    $HYPRLAND --i-am-really-stupid -c /tmp/pad-hyprland.conf > "$OUT/hypr.log" 2>&1 &
fi

i=0
READY=no
while [ $i -lt 90 ]; do
    if tr '\r' '\n' < "$OUT/hypr.log" | grep -q "Using: OpenGL"; then
        READY=log
        break
    fi
    pgrep -x hyprland >/dev/null 2>&1 || { echo "hyprland exited early"; break; }
    sleep 5
    i=$((i + 1))
done
echo "=== readiness: $READY after ~$((i * 5))s (hyprland alive=$(pgrep -x hyprland >/dev/null 2>&1 && echo yes || echo no))"

echo "=== outputs/monitors the compositor reported ==="
tr '\r' '\n' < "$OUT/hypr.log" | grep -E "New aquamarine output|Added new monitor|Registered global \[WLOutput|Applying monitor rule|Created headless output|SHM: allocated" | tail -12

WL=$(sed -n 's/.*Running on WAYLAND_DISPLAY: //p' "$OUT/hypr.log" 2>/dev/null | tail -1)
if [ -z "$WL" ]; then
    for c in "$XDG_RUNTIME_DIR"/wayland-*; do
        [ -S "$c" ] && WL=$(basename "$c") && break
    done
fi
export WAYLAND_DISPLAY="${WL:-wayland-1}"
echo "=== WAYLAND_DISPLAY=$WAYLAND_DISPLAY ==="

# let it render: the output buffer copy-back (patch 0002) drops frame.raw
k=0
while [ $k -lt 30 ]; do
    [ -s "$OUT/frame.raw" ] && { echo "frame.raw appeared after ~$((k * 5))s ($(stat -c %s "$OUT/frame.raw") bytes)"; break; }
    sleep 5
    k=$((k + 1))
done

grim_try() {
    for c in "$WAYLAND_DISPLAY" wayland-0 wayland-1 wayland-2; do
        [ -S "$XDG_RUNTIME_DIR/$c" ] || continue
        WAYLAND_DISPLAY=$c timeout 300 grim -o "$HYPR_PAD_HEADLESS_OUTPUT" "$1" && { echo "grim ok on $c"; export WAYLAND_DISPLAY=$c; return 0; }
        echo "grim -o $HYPR_PAD_HEADLESS_OUTPUT failed on $c (exit $?)"
        WAYLAND_DISPLAY=$c timeout 300 grim "$1" && { echo "grim (no -o) ok on $c"; export WAYLAND_DISPLAY=$c; return 0; }
        echo "grim (no -o) failed on $c (exit $?)"
    done
    return 1
}

echo "=== grim (screencopy, shm) ==="
grim_try "$OUT/shot.png"
echo "grim exit=$?"
ls -la "$OUT/shot.png" "$OUT/frame.raw" 2>&1 | tail -3

echo "=== grim second frame ==="
grim_try "$OUT/shot2.png"
echo "grim2 exit=$?"
ls -la "$OUT/shot.png" "$OUT/shot2.png" "$OUT/frame.raw" 2>&1 | tail -4

echo "=== rendering stats from the log ==="
tr '\r' '\n' < "$OUT/hypr.log" | grep -nE "Using:|Vendor:|Renderer:|Supported EGL|EGL:|eglDeviceFromDRMFD|shm:|Swapchain:|SHM:|headless|GBM|Framebuffer" | tail -40

echo "=== stopping hyprland ==="
pkill -TERM -x hyprland 2>/dev/null
sleep 10
pkill -KILL -x hyprland 2>/dev/null
pkill -KILL -x script 2>/dev/null
sleep 2

for f in "$XDG_RUNTIME_DIR"/hypr/*/hyprland.log; do
    [ -s "$f" ] && { cp "$f" "$OUT/hyprland-instance.log"; echo "instance log: $(wc -l < "$f") lines"; }
done

echo "=== hypr.log (tail) ==="
tr '\r' '\n' < "$OUT/hypr.log" | tail -30
echo "=== hypr.log size: $(wc -c < "$OUT/hypr.log") bytes, lines: $(wc -l < "$OUT/hypr.log") ==="
cp "$OUT/shot.png" /tmp/shot.png 2>/dev/null || true
cp "$OUT/frame.raw" /tmp/frame.raw 2>/dev/null || true
cp "$OUT/hypr.log" /tmp/hypr.log 2>/dev/null || true
ls -la "$OUT"
echo GATE04_PROBE_DONE
