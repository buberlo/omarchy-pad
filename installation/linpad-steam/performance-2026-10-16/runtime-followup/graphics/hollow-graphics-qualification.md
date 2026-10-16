# Hollow Knight graphical qualification after build 4447

Read-only source/log analysis. Commands below are prepared for the parent task; none were executed on the device by this agent. Build 4447 fixes procfs stack-map boundaries. That fix and the local Mono passes do not establish a visible game window or a performance result.

## Retain the known graphics route

The current translator hook, `serve/fex-hollow-egl`, uses the patched FEX binary, Mesa 25 software EGL for Unity/SDL window bootstrap, and the independent Mesa 26 Venus Vulkan ICD for rendering. It sets:

```text
XDG_RUNTIME_DIR=/tmp/xdg-runtime
WAYLAND_DISPLAY=wayland-0-gpu
SDL_VIDEODRIVER=wayland
EGL_PLATFORM=wayland
MESA_LOADER_DRIVER_OVERRIDE=swrast
LIBGL_ALWAYS_SOFTWARE=1
GALLIUM_DRIVER=llvmpipe
VK_ICD_FILENAMES=/opt/hollow-mesa26/usr/share/vulkan/icd.d/virtio_icd.json
VK_LOADER_LAYERS_DISABLE=*
MESA_VK_WSI_DEBUG=sw
```

The game arguments select `-force-wayland -force-vulkan`, currently request 960×540 and windowed mode, and add timestamps. Auxiliary llvmpipe EGL does not prove that the game's final renderer is software: preserve Unity's actual selected physical-device/renderer lines as evidence. Earlier runs selected `Virtio-GPU Venus (Apple M2 GPU)`, but stalled or failed before a qualified game frame.

Keep `MESA_VK_WSI_DEBUG=sw`. `wl-bridge/src/dmabuf.c` advertises feedback but explicitly rejects actual DMA-BUF import. `gpu/DESIGN.md:185` documents the present path as three copies: Vulkan image → guest wl_shm, guest compositor → view buffer, then the native bridge's copy. Removing the software WSI transport is not an available zero-copy optimization.

The old trace (`reports/hollow4445-egl-trace-status-5.txt`) observed an early 1180×780 window and scale 1 even with the requested 960×540 arguments; the output itself advertised 2360×1560 at scale 2. That was before successful final display initialization. Read the new actual swapchain/window dimensions before making a resolution comparison. Prefer `InitializeOrResetSwapChain …` if Unity emits it, and confirm the compositor buffer dimensions or an explicit swapchain observer. Do not infer the render extent from the command line or display mode alone.

## First-window observations

Read-only guest commands, adapting the Unity log basename to the current run:

```sh
tail -100 /tmp/hollow4447-egl-unity.txt
tail -100 /tmp/ishwl.log
ls -l /tmp/ishwl/v*.buf
```

Useful Unity milestones are a nonzero `Display 0`, selected Venus device, graphics queue/device creation and a successful swapchain creation/reset. The compositor's `map toplevel N 'Hollow Knight'` identifies the game view. `view N: no ack for frame S, resending` indicates its 1.5-second acknowledgement timeout, not successful smooth presentation. A window or changing frame file still needs a physical capture showing actual game content and an input response before gameplay can be claimed.

The PSO cache-not-found line is an ordinary first-run cache miss; earlier failure analysis identified a separate invalid display pointer, not a missing-cache defect. Do not change Vulkan features or shader caches without a concrete new error. Do not read the `notify` or `events` FIFOs for observation: that consumes bridge messages.

## Per-game view cadence with existing instrumentation

`wl-bridge/src/main.c:256` reads `ISHWL_FRAMELOG` once at compositor startup. `ishwl-session` sources `/etc/ishwl/session.d/*.sh` before starting the compositor. On a deliberately fresh desktop session, a unique temporary session hook can enable the existing log without an app rebuild:

```sh
test ! -e /etc/ishwl/session.d/99-hollow-frame-capture.sh || exit 73
printf '%s\n' 'export ISHWL_FRAMELOG=/tmp/hollow4447-frames.txt' \
  > /etc/ishwl/session.d/99-hollow-frame-capture.sh
```

This has no effect on an already running compositor. Do not restart a live game merely to enable it. The hook may be removed after that compositor has started; the current process keeps its open log until its session ends. The log records every composited view, so it must be filtered by the observed Hollow Knight view ID. It is line-buffered and formatted:

```sh
# After the new compositor has created the expected log:
test -f /tmp/hollow4447-frames.txt && \
  rm /etc/ishwl/session.d/99-hollow-frame-capture.sh
```

```text
MONOTONIC_MS VIEW_ID FRAME_SEQUENCE DAMAGE_WIDTH DAMAGE_HEIGHT
```

`view.c:199–212` records the line after compositing and before notifying the native bridge. The next damaged frame waits for the previous native acknowledgement (`view.c:163–170,232–239`). Consequently this is useful downstream **game-view delivery cadence**. It is not GPU render duration or independent physical-display refresh timing. Damage width/height is not necessarily the full swapchain extent. The millisecond timestamp wraps after roughly 49.7 days; interval analysis must use unsigned 32-bit subtraction.

After a visible, stable scene is selected, capture a bounded 30-second slice rather than startup or a menu-to-game transition:

```sh
frame_log=/tmp/hollow4447-frames.txt
capture=/tmp/hollow4447-frame-window
test -f "$frame_log" || exit 66
start_line=$(wc -l < "$frame_log")
start_second=$(date +%s)
sleep 30
end_second=$(date +%s)
end_line=$(wc -l < "$frame_log")
printf 'start_second=%s end_second=%s start_line=%s end_line=%s\n' \
  "$start_second" "$end_second" "$start_line" "$end_line" > "$capture.meta"
if [ "$end_line" -gt "$start_line" ]; then
  sed -n "$((start_line + 1)),${end_line}p" "$frame_log" > "$capture.txt"
else
  : > "$capture.txt"
fi
```

Select the exact numeric view ID from the map log; never sum all views. Report its row count over the captured elapsed wall-clock window, plus timestamp/sequence gaps and interval quantiles. The shell metadata uses whole seconds, so the window rate is approximate; retain that resolution in the result. The cadence `(rows-1)/(last_timestamp-first_timestamp)` describes only the active span and must not replace the full-window rate: two early frames followed by a long stall must not become a good FPS result. Sequence gaps, view replacement, resize or acknowledgement-timeout messages invalidate a simple uninterrupted-scene comparison and should be reported explicitly. Keep a physical start/end capture to establish which scene/window the sample belongs to.

After copying the two capture files to the Mac, run the prepared host-only parser with the observed game view ID:

```sh
python3 work/performance/parse-hollow-frame-window.py \
  work/performance/reports/hollow4447-frame-window.txt \
  work/performance/reports/hollow4447-frame-window.meta \
  --view OBSERVED_NUMERIC_GAME_VIEW_ID
```

It rejects malformed rows, excludes other views, handles 32-bit timestamp/sequence wrap, reports full-window and active-span rates separately, and flags missing frames or sequence discontinuities. It does not infer full-frame dimensions from damage rectangles. The view ID must be supplied from the actual log, not guessed. The parser's synthetic checks are arithmetic/format validation only, never device performance evidence.

The native app's `linux-gui-stats.txt` is unsuitable for this measurement. `LinuxGUIBridge.swift:870–922` shares a single burst counter across views and completes a burst only after a >0.5-second gap followed by another frame. It is not a current per-game FPS counter.

## Actual Vulkan swapchain timing: additional diagnostic required

Do not reuse `vulkan-present-timing.c` for Unity unchanged. It is intentionally a single-device/single-queue cube proxy, uses one global real-present function, combines all successful calls, does not record swapchain identity or extent, and only reports at `atexit`. An externally terminated Unity process may emit no summary. The first physical cube-proxy attempt also failed; later local forwarding tests did not qualify physical presentation.

A suitable optional game observer should intercept the loader's proc-address paths and:

1. Retain correct dispatch separately for devices/queues; preserve unrelated pointers and every result.
2. Track each successful `vkCreateSwapchainKHR` with its handle, creation generation, surface, extent, format and present mode, and remove it on destroy.
3. Count each presented swapchain separately, honoring per-swapchain results when provided. Preserve failed/suboptimal calls and timing gaps instead of hiding them.
4. Begin a bounded sample only on an explicit scene marker, exclude a stated warmup, and write a final sample summary when the time window ends rather than requiring game exit. No per-frame file logging should be necessary.
5. Report successful present-return cadence as that metric, with the corresponding view delivery capture and physical observation. It is still not a direct display-refresh measurement.

The current `fex-hollow-egl` replaces `LD_LIBRARY_PATH` and scrubs `LD_PRELOAD`. A future diagnostic must be inserted by an explicit one-run translator hook **after** that sanitization; prepending environment variables outside it will not work. No such game observer or hook has been implemented or device-tested in this audit.

Once the game visibly renders, the first useful performance comparison is the same scene at two verified swapchain extents, with the same Vulkan route and foreground state. The copy path gives a concrete reason to test resolution. No performance improvement is claimed until both samples exist.
