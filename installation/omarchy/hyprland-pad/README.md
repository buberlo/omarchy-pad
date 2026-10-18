# hyprland-pad — Hyprland on a headless, render-node-only host (Omarchy Pad gate 0.4)

Hyprland 0.56.2 and Aquamarine 0.15.1, patched so that the compositor starts and renders
frames on a **headless** output when the only DRM device is a render node
(`/dev/dri/renderD128`, no KMS card) and dma-buf/EGLImage import is impossible — the
situation inside the iSH-ARM64 emulator on iPad/macOS.

Result (2026-10-17, evidence in `work/omarchy-pad/evidence/gate-0.4/`):

    Using: OpenGL ES 3.0 Mesa 26.2.4-arch1.1
    Vendor: Mesa
    Renderer: llvmpipe (LLVM 23.1.1, 128 bits)
    Added new monitor with name HEADLESS-1 ... size [1280, 800]
    grim -o HEADLESS-1 /root/gate04-out/shot.png  ->  exit 0, 829610 bytes
    png-stats.py shot.png frame.raw               ->  NON-BLACK FRAME
        PNG: 1269 distinct colours, 98.03 % non-black pixels, mean luma 20.09
        raw: 1230 distinct colours, 100 % non-black pixels, mean luma 19.89

The GLES 3.0 context is **llvmpipe**, not zink: zink cannot create an EGL display in the
guest (`MESA: error: ZINK: vkCreateInstance failed (VK_ERROR_INCOMPATIBLE_DRIVER)` →
`eglInitialize failed`) on any platform, see `03-egl-platforms.txt`. The patches are
renderer-agnostic, so the same binaries use zink wherever it works.

## Patches

`patches/aquamarine/aquamarine-0001-headless-render-node.patch`

* `CHeadlessBackend::drmRenderNodeFD()` opens `AQ_HEADLESS_DRM_NODE` (default
  `/dev/dri/renderD128`) and reports it, so the compositor gets a DRM fd for its EGL/GBM
  context even when there is no card node. `drmFD()` deliberately stays `-1`: nothing can
  be scanned out and no GBM allocator should be built from a headless backend.
* New `CSHMAllocator`/`CSHMBuffer` (`include/aquamarine/allocator/SHM.hpp`,
  `src/allocator/SHM.cpp`): `memfd_create` + `ftruncate` + `mmap`, `BUFFER_TYPE_SHM`,
  `BUFFER_CAPABILITY_DATAPTR`, no dmabuf at all. Used for headless output swapchains and
  as `CBackend::start()`'s last-resort allocator, which removes the
  `Cannot open backend: no allocator available` failure. `AQ_HEADLESS_GBM=1` restores the
  old behaviour.
* `CBackend::start()` falls back to the shm allocator when no implementation reports a
  DRM fd and a headless implementation is present.
* **iSH timerfd workaround**: iSH's `sys_timerfd_settime()` holds `fd->lock` and then
  waits for the timer lock, while `timerfd_callback()` runs on the timer thread holding
  the timer lock and waits for `fd->lock` (`kernel/time.c` + `util/timer.c`) — the first
  frame timer re-arm deadlocks the compositor forever. When iSH is detected (`uname`
  release contains `-ish`, or `AQ_HEADLESS_NO_TIMERFD=1`; `AQ_HEADLESS_TIMERFD=1` forces
  the old behaviour) idle events are delivered through a pipe instead of a timerfd, and
  the headless frame timer is replaced by an idle event.

`patches/hyprland/hyprland-0001-egl-render-node-device.patch`

* `eglDeviceFromDRMFD()` also matches `EGL_DRM_RENDER_NODE_FILE_EXT`
  (`EGL_EXT_device_drm_render_node`), so `EGL_PLATFORM_DEVICE_EXT` can select a render-node
  device on hosts that have no card node.
* `initEGL()` returns `bool` instead of `RASSERT`ing, so a failing platform falls through
  device → GBM instead of aborting the compositor.

`patches/hyprland/hyprland-0002-shm-output-buffer.patch`

* An output buffer that is not a dmabuf (`BUFFER_TYPE_SHM`) gets a GL texture/FBO
  (`CGLRenderbuffer`) instead of an EGLImage, and `endRender()` copies the finished frame
  back with `glReadPixels` (`IFramebuffer::readPixelsToBuffer`, reusing the screencopy
  readback code). `HYPR_PAD_SHM_DUMP=<path>` additionally dumps the raw frame, which is
  how the gate proves the pixels really reached the buffer.

`patches/hyprland/hyprland-0003-headless-output-env.patch`

* `HYPR_PAD_HEADLESS_OUTPUT=<name>` creates a headless output after the managers are up
  (deferred to the first event loop iteration). Needed because Hyprland only creates
  headless outputs on request and `hyprctl` cannot be used under iSH: iSH does not
  implement `setsockopt(SO_RCVTIMEO)`, so hyprctl aborts with
  `Couldn't set socket timeout (2)` before it sends anything.

`patches/hyprland/omarchy/` carries the four patches Omarchy's own hyprland package
applies (software-renderer classification, NVIDIA EGL teardown, wl_output global grace,
monitor rule vrr); the gate patches are generated on top of them.

## Building

    sh build.sh all            # aarch64 Arch container "gate04-build", native speed
    # -> work/omarchy-pad/gate-0.4/pkgs/{aquamarine-pad-0.15.1-1,hyprland-pad-0.56.2-1}-aarch64.pkg.tar.zst

`build.sh` drives Docker (`colima-linpad-fex`), runs `makepkg` as an unprivileged user and
copies the packages back. Notes learned the hard way: pacman needs `--disable-sandbox`
inside Docker (Landlock is blocked by seccomp), the aarch64 Arch mirrors run at ~15 KB/s
(use `mirror.sjtu.edu.cn`, ~2 MB/s), and macOS is case-insensitive so the package's
`usr/bin/Hyprland` and its symlink `usr/bin/hyprland` cannot both exist on disk — the
binary is installed as `usr/bin/hyprland` while `meta.db` maps both guest paths to it.

## Running the gate

    sh run-gate04.sh [zink|llvmpipe]

Installs both packages into `$FS` (default `work/omarchy-pad/fs-gate04`), boots the
GPU-enabled CLI and runs `guest/gate04-probe.sh` inside the guest, then copies the logs,
PNG and raw frame into `work/omarchy-pad/evidence/gate-0.4/` and prints the pixel
statistics.

**Important**: a fakefs is a real directory tree *plus* `meta.db`, and iSH resolves every
path through `meta.db`. Files merely dropped into `FS/data` are invisible in the guest —
`bsdtar -xf pkg.tar.zst -C FS/data` installs nothing. `install-into-fakefs.py` extracts a
package and registers every entry (and fixes `meta.db_inode`, otherwise iSH rebuilds the
database on boot):

    python3 install-into-fakefs.py --fs <fakefs> [--register /guest/path]... <pkg.tar.zst>...

## Guest configuration

`guest/pad-hyprland.conf` is deliberately minimal: one `monitor = HEADLESS-1,
1280x800@60` rule, `debug:damage_tracking = 0` (full frames, so the shm copy-back always
writes a complete image), `debug:disable_logs = false` (Hyprland stops logging after a few
lines otherwise), `debug:enable_stdout_logs = true`, and Xwayland disabled.
`guest/gate04-probe.sh` runs the compositor under `script(1)` so the redirected log is
line buffered, waits for `Using: OpenGL`, then screencopies with `grim`.
