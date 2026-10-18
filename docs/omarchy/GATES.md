# Omarchy Pad — gate log

Every gate in the plan writes its evidence to `work/omarchy-pad/evidence/` before the next
one starts. This file is the index: what was run, what it produced, and what is still open.
Paths are relative to this repository unless they start with `/Users/konrad/omarchy-pad`.

The build tree lives at **`/Users/konrad/omarchy-pad`** (space-free). Paths containing a
space break `tools/staticdefine.sh`, which splits the compile command with `str.split()`;
that script now uses `shlex`, but the whole toolchain (meson, xcodebuild, docker paths) is
kept space-free anyway.

| Gate | What it proves | Status | Evidence |
| --- | --- | --- | --- |
| 0.1 | macOS `ish` CLI + `fakefsify` build and run | **pass** | `build-arm64-release/ish`, `build-arm64-release/tools/fakefsify`; `python3 -m mako`/`ziglang` in `work/tools/venv` |
| 0.1b | The CLI + fakefs + boot pipeline works at all | **pass** | `work/omarchy-pad/evidence/01-alpine-control.txt` — Alpine 3.21 aarch64 boots, `ALPINE_CONTROL_OK`; note: `fakefsify <tar> <dir>` makes `<dir>` itself the fs-control directory |
| 0.2 | An Arch Linux ARM + Omarchy image builds and boots | **pass** | `installation/omarchy/build-image.sh` → `work/omarchy-pad/omarchy-aarch64-pad.tar.gz` (897 MB, 448 packages, version 202610171543); boot evidence `work/omarchy-pad/evidence/boot-omarchy-aarch64-pad.txt` (`/etc/os-release` = Omarchy, glibc 2.43, `quickshell --version` answers, `hyprland` starts) |
| 0.3 | Guest graphics capability | **pass** | `work/omarchy-pad/evidence/gate-0.3/gpu-probe.txt`: `/dev/dri/renderD128` exists; **zink on Venus works** (`zink Vulkan 1.4(Virtio-GPU Venus (Apple M3 Pro) (MOLTENVK))`) and reports **GLES 2.0**, becoming **GLES 3.0 with `MESA_GLES_VERSION_OVERRIDE=3.0`**; llvmpipe reports **GLES 3.2** as the software fallback |
| 0.4 | Hyprland renders a frame with a headless output | **in progress** | `installation/omarchy/hyprland-pad/`: patches written and applied on Hyprland 0.56.2 + Aquamarine 0.15.1 (headless render-node fd + SHM allocator; EGL device from the render node; shm render target with readback). Packages built: `work/omarchy-pad/gate-0.4/pkgs/{aquamarine-pad-0.15.1-1,hyprland-pad-0.56.2-1}-aarch64.pkg.tar.zst`. They are now baked into the image by `installation/omarchy/build-image.sh`. |
| 0.5 | Systemd-less Omarchy session (supervisor, shims, Quickshell) | **partial** | `work/omarchy-pad/evidence/gate-0.5/session-probe.txt`: init starts the buses/daemons, and every shim answers (systemctl verbs, systemd-run, systemd-cat, uwsm-app, journalctl, loginctl, hostnamectl, timedatectl, udevadm, ufw, bluetoothctl, rfkill, brightnessctl). **`omarchy-hw-platform` now answers `aarch64`** after the procfs magic-link fix below. Quickshell itself is next. |
| 0.6 | `omarchy-pad-bridge`: screencopy frames into ishwl + input injection | **pass** | `work/omarchy-pad/evidence/gate-0.6/`: mock-compositor chain, real-Hyprland chain, and `on-screen/` — a clean app boot of image `202610180603` in the Simulator puts the whole desktop on the canvas: session started by the app's own launch request, Hyprland on `wayland-1`, Omarchy's Quickshell shell mapped as the `omarchy-bar` layer, a **real `foot` window composited into the frame** (with Hyprland's border and the cursor) at the iPad's own 1024x1366 size. |
| 0.7 | iPad text input / IME into the guest | **partial** | the pointer path is proven end to end: `motion 1 700 900` on the app's own events channel leaves the composited cursor at x 700..711, y 901..920 in the captured frame (`on-screen/on-screen-clean-boot.txt`). Keyboard/IME from the canvas is not measured yet. |
| 0.8 | Simulator end-to-end + performance/memory numbers | **partial** | the app builds (device + simulator), 408 DesktopKit tests pass, it imports the Arch/Omarchy rootfs and boots Linux (`import_seconds=14.55`, `boot_seconds=18.48`), starts the session, and shows the running desktop. Measured: **~1 fps** at 1024x1366 (467 frames in ~8 minutes), 5.5 MB per frame, `capture_failures=0`. Memory/CPU numbers and the device run are still open. |

## Engine changes carried or made so far

| Change | File | Why |
| --- | --- | --- |
| `clone3` is a silent ENOSYS stub | `kernel/arch/arm64/calls.c` | glibc/musl fall back to `clone` on every thread start; the loud stub logged a diagnostic per call |
| `capget`/`capset` implement the Linux capability ABI | `kernel/getset.c` | they returned 0 without writing the caller's buffer, so libcap read uninitialised memory; root now reports a full set and a drop is a no-op |
| `staticdefine.sh` survives spaces in the build path | `tools/staticdefine.sh` | `str.split()` tore `-I"/Users/…/omarchy ipad/…"` into two tokens |
| **Opening `/proc/<pid>/fd/<n>` duplicates the descriptor** | `fs/proc.c`, `fs/generic.c`, `fs/proc.h` | The magic link used to be followed to its textual target (`pipe:[12]`), which cannot be opened, so **bash process substitution never worked** in iSH. Omarchy's own `omarchy-hw-platform` starts with `read -r owner mode < <(stat …)`, and its platform detection failed until this was fixed; `read x < <(cmd)` now works, as does `/dev/fd/N` and `/dev/std*`. |
| **Host-side opens no longer crash without a current task** | `fs/generic.c` | The app's Objective-C helpers (`FsIsAlpineRoot` → `read_file("/etc/apk/world")`) run while the UI loads, before the kernel has a task; `path_normalize` dereferenced `current` and the app died with SIGSEGV at launch (simulator crash report `iSH ARM64-2026-10-18-003136.ips`). `generic_openat` answers ENOENT when `current == NULL`. |
| `systemd-cat` shim consumes option arguments | `omarchy-pad/files/usr/local/lib/omarchy-pad/shim` | `systemd-cat -t tag cmd` used to exec `tag`; Omarchy launches Quickshell through it |
| **Timer callbacks run without the timer lock** | `util/timer.c` | `timerfd_settime` held `fd->lock` and waited for the timer lock while the timer thread held the timer lock and took `fd->lock` in `timerfd_callback` — an ABBA deadlock that froze the emulator the first time a compositor re-armed its frame timer. Found by the Hyprland workstream with `sample`; the callback now runs with the timer lock released. |
| Case-only path collisions are resolved at image build time | `installation/omarchy/resolve-case-collisions.sh` | Arch ships 64 case-only duplicate paths (terminfo, man aliases, xtables, perl, `Hyprland`/`hyprland`). The iPad's case-insensitive container cannot hold both and the importer died with `EEXIST` on the first pair. The resolver materialises the lowercase name when it is a symlink to the dropped one (so `/usr/bin/hyprland` survives as a real binary) and fails the build if any pair remains. |
| The importer names the entry it failed on | `tools/fakefs.c` | `POSIX_ERR()` reported only "File exists" plus a line number; the failing path is now part of the message, which is how the collision above was found. |
| iSH `prctl` breadth (fork, not ours) | `kernel/misc.c` | `PR_SET_NO_NEW_PRIVS`, `SET_DUMPABLE`, `SET_PDEATHSIG`, `CAPBSET_READ`… already answered by commit `64f54210` |

## Gate results (updated after the execution round)

- **0.4 — pass.** Patched Hyprland 0.56.2 + Aquamarine 0.15.1 start with a headless output,
  allocate memfd (shm) swapchain buffers and render: `grim -o HEADLESS-1` writes a non-black
  PNG (1269 colours, 98 % non-black) that matches the compositor's own shm dump
  colour-for-colour. GL was llvmpipe in that run (zink's `vkCreateInstance` failed while a
  second emulator was running; the patches select zink automatically when it initialises).
  Patches, PKGBUILDs and the guest probe: `installation/omarchy/hyprland-pad/`, evidence in
  `work/omarchy-pad/evidence/gate-0.4/`.
- **0.6 — pass.** `omarchy-pad-bridge` against the real patched Hyprland: ishwl maps a
  toplevel app-id `omarchy-pad`, `frames.log` streams at 1280x800 and 1024x768 after a
  resize, the view buffer holds a real frame, injected `motion 1 700 500` moves Hyprland's
  cursor to exactly 700,500, the F5 bind fires, the 35,572-byte XKB keymap crosses intact,
  and `configure` resizes the output through Hyprland's IPC. Real-Hyprland throughput under
  emulation is ~0.7–1 fps at 1280x800 (the mock path does ~40 fps): the render path is the
  Phase-2 performance work. Evidence in `work/omarchy-pad/evidence/gate-0.6/`.

### 0.6 on screen — the desktop in the app (image `202610180603`)

A clean run: install the app, let it import the image it carries, launch it, and do nothing
else. The canvas shows the Omarchy desktop. Five bugs stood between the previous run (a
black window, "Starting Omarchy…" forever) and this one, all found by watching the guest
from the Mac through the launch channel (`installation/omarchy/send-guest-cmd.py`):

1. **The capture froze when nothing changed.** `copy_with_damage` completes a request only
   for damage Hyprland counts as output damage, and cursor motion did not: a pending request
   sat unanswered while the pointer moved, and the canvas kept an old frame. The bridge now
   takes a **full copy** every frame (`hypr.c`), and the pending-request timeout applies to
   every request instead of only the first, so a wedged request cannot freeze the stream.
2. **`hyprctl` could not talk to the compositor.** It sets `SO_RCVTIMEO`/`SO_SNDTIMEO` with
   the *Linux* option numbers (20/21); iSH only knew its own historical numbers (66/67), so
   the call failed with EINVAL and hyprctl aborted every request — the compositor's readiness
   checks and Omarchy's own tooling along with it (`fs/sock.h`, `fs/sock.c`).
3. **The session's clients attached to the wrong compositor.** ishwl owns `wayland-0`, and
   Hyprland inherits the session's environment, so everything Hyprland started — Omarchy's
   shell through `autostart.lua`, every `exec-once`, every app — connected to *ishwl* instead
   of to Hyprland. Nothing appeared in the composited output. The session now clears stale
   sockets and starts Hyprland on an explicit `wayland-1`, which its children inherit.
4. **The bridge looked for Hyprland on the wrong socket too.** It defaults `--ishwl-socket`
   to `$WAYLAND_DISPLAY`, which is now Hyprland's, and then excludes that name from the
   candidates it tries as the compositor. The session passes `--ishwl-socket` explicitly and
   captures the value before it replaces `WAYLAND_DISPLAY`.
5. **The iPad's size was ignored.** `hyprctl keyword monitor …` is refused by Hyprland 0.56
   ("keyword can't work with non-legacy parsers. Use eval."), so the output stayed at the
   placeholder 1280x800 and the canvas stretched it. The bridge now sends
   `eval hl.monitor({ output = …, mode = …, position = "0x0", scale = 1 })` over Hyprland's
   IPC socket, with `keyword` and then `hyprctl` as fallbacks, and rejects a reply that
   contains Hyprland's own words for a refusal.

What the run proves, in one line: the app's own `spawn` starts the session; Hyprland answers
in 4 s on `wayland-1`; the bridge resizes the output to the iPad's 1024x1366 and presents
frames at ~1 fps with `capture_failures=0`; Omarchy's shell is mapped as the `omarchy-bar`
layer (26 px, full width); a real `foot` window appears in the canvas with Hyprland's border
and the composited cursor; and `motion 1 700 900` through the app's own events channel leaves
the cursor at exactly 700,900 in the captured frame.

Still open here: the bar's widget content and the wallpaper are not painted yet (the bar's
background band is), and the frames are one-tenth of a second-per-frame short of usable —
Phase 2 work.

## Known image facts

- **A fakefs only sees files its metadata knows about.** `meta.db` maps every path to an
  inode; dropping a file into `<fakefs>/data/...` from the Mac leaves it invisible in the
  guest (ENOENT, no diagnostic). Guest-written files are registered automatically; for
  host-side package installation use
  `installation/omarchy/hyprland-pad/install-into-fakefs.py`, which extracts a package and
  inserts the `paths`/`stats` rows and keeps `meta.db_inode` in sync.

- **systemd cannot run** (no cgroups, no `/proc/self/cgroup`, no `unshare`/`setns`, no
  kobject-uevent netlink, `mount()` rejects everything outside a short flag list). `/sbin/init`
  is `omarchy-pad-init`.
- **pacman** on the iPad path needs `DisableSandbox` and no `DownloadUser` (Landlock is
  stubbed); the image is built off-device, so in-guest `pacman -Syu` is a later convenience,
  not a gate.
- **Case collisions**: three legacy netfilter header pairs are allowlisted and reported;
  anything else fails `installation/omarchy/check-case-collisions.sh`.
- **Rootfs size**: 897 MB compressed, ~2.5 GB imported, 448 packages. The full Omarchy
  default set is bigger; this is the slim manifest in `installation/omarchy/packages.txt`.

## Open bug: `execve` from a guest process returns ENOTDIR (app guest only)

Observed in the app's guest, ishwl's spawn path (`/tmp/ishwl.log`):

```
ishwl 36452.466: spawned 15248: omarchy-pad-session
ishwl 36452.468: execl /usr/local/bin/ishwl-launch failed: Not a directory
ishwl 36452.469: execl /bin/sh failed: Not a directory
ishwl 36452.470: execl /usr/bin/bash failed: Not a directory
ishwl 36452.470: execl /usr/local/bin/pad-exec failed: No such file or directory
ishwl 36453.099: child 15248 exited (32512)
```

Facts that bound it:

- The spawn message arrives; the child then fails **every existing target with ENOTDIR**, while a
  non-existent path correctly reports ENOENT — so path resolution finds the files and `execve`
  rejects them afterwards.
- `/usr/bin/bash` is a plain ELF with no symlink in its path, and it fails the same way, so this is
  not the merged-`/usr` symlink chain.
- The same `ishwl` binary, the same image tree and the same `spawn` line **succeed in the CLI
  guest** (`pad-spawn-probe` ran; `omarchy-pad-session` reached "starting Hyprland").
- The emulator's own boot exec of `/sbin/init` (a `#!/bin/sh` script reached through `/sbin` and
  `/bin` symlinks) succeeds in the same app guest.

The difference is the caller: a **forked guest process** calling `execve` (ishwl after `fork()` +
`setsid()`), versus the emulator's own exec. Likely areas: the `fs_info` the child inherits through
`fork()` (root/pwd fds) in `kernel/exec.c`, or shebang/ELF loading when the caller is a guest task.

Workarounds, in order of preference:

1. Fix the emulator path (investigate `sys_execve`/`do_execve` with an inherited `fs_info`).
2. Let the supervisor (`omarchy-pad-init`, whose exec path is known-good) own a small request FIFO
   so `spawn` becomes a request to PID 1 instead of a guest `execve` from ishwl.

### The launch channel (workaround 2, in place)

`omarchy-pad-init` creates `/run/omarchy-pad/launch` (a FIFO) and starts one `setsid sh -c <request>`
per line; `ishwl`'s `spawn_command()` writes a one-line request instead of `exec`ing. Four details
are what make it work, and three of them were bugs the first time round:

- **The reader must survive writer EOF.** A bare `while read; done <fifo` ends as soon as the first
  writer closes, so the first launch would also have been the last. The loop is wrapped in
  `while :; do … done <fifo`, which reopens the FIFO (blocking until the next writer).
- **The child needs ishwl's environment, not PID 1's.** The supervisor runs the request, so nothing
  is inherited implicitly: `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`, the session bus address, the
  toolkit variables and the scm-compat `LD_PRELOAD` would all be lost. `spawn_command()` therefore
  writes a self-deleting launcher script (`/run/omarchy-pad/spawn-<pid>.sh`) that `export`s its own
  environment and then runs the command, and sends that path over the FIFO. Values with quotes and
  spaces survive (single-quote escaping, checked by the test below). A request that cannot be
  delivered is removed again before the direct `exec` fallback runs.
- **A request must not be lost in the gap between readers.** The writer retries its open: `ENXIO`
  (channel exists, no reader attached yet) is worth up to 5 s because the supervisor's reopen is
  imminent; `ENOENT` (no channel at all — a run without a supervisor) only 500 ms, then the direct
  exec path takes over. Both bounded: a launch can never hang the compositor.
- **The session must not depend on being started by ishwl.** `omarchy-pad-session` sets
  `WAYLAND_DISPLAY` and `XDG_RUNTIME_DIR` defaults that match `ishwl-session`, so a manual run (and
  the supervisor path) land on the same compositor, and it writes what it is doing to
  `/tmp/omarchy-pad/status` (the canvas shows it) and failures to `/tmp/omarchy-pad/error`.

The channel is checked without an emulator by
`installation/omarchy/launch-channel-test/run.sh`, which extracts the supervisor loop and
`write_launch_script()` from the real sources, cross-compiles the writer for aarch64 Linux and runs
both in a container: sequential requests across writer EOF, the `ENOENT` and `ENXIO` races, the
bounded no-supervisor failure, environment carry-over with quotes, and no leftover scripts. Evidence:
`work/omarchy-pad/evidence/gate-0.6/launch-channel.txt`.
