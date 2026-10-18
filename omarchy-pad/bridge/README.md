# omarchy-pad-bridge

The guest program that turns a **headless Hyprland** session into the surface LinPad's
native iPad window system can show, and injects the iPad's input back into Hyprland.

One process, two Wayland connections:

| Side | What it is | What the bridge uses |
| --- | --- | --- |
| **Hyprland** | the compositor that owns the session; the bridge is a client of it | `zwlr_screencopy_manager_v1` (capture with `overlay_cursor=1`, `copy_with_damage`), `zwlr_virtual_pointer_manager_v1` v2 (`motion_absolute`), `zwp_virtual_keyboard_manager_v1` (keymap + key events), `wl_output` (finds `HEADLESS-1`) |
| **ishwl** | LinPad's small compositor, which hands every `xdg_toplevel` to the iOS app as a native DesktopKit window | an ordinary client: one `wl_surface` + `xdg_toplevel` with app_id **`omarchy-pad`** and title `Omarchy`, `wl_shm` buffers, `wl_seat` (the iPad's pointer and keyboard come back over it) |

## The shared-buffer trick

Each frame buffer is **one file** with **two `wl_buffer` objects**, created from duplicated
fds of the same `wl_shm_pool`-backed file — one on Hyprland's connection (the screencopy
destination) and one on ishwl's (the surface buffer).  Hyprland writes the screen into
that memory; ishwl reads the same pages when the surface is committed.  The bridge never
touches a pixel.

```
Hyprland ──screencopy──▶ /tmp/omarchy-pad/shm/frame-<pid>-<n>-<w>x<h>-<s>.buf ◀──wl_surface── ishwl ──▶ iPad
     ▲                                                                                              │
     └──────────── zwlr_virtual_pointer / zwp_virtual_keyboard ◀── wl_seat ◀── events FIFO ◀────────┘
```

Files, not `memfd_create`: iSH stubs `memfd_create` (`kernel/arch/arm64/calls.c`), and a
fakefs file is a host file, so a `MAP_SHARED` mapping of it is the same memory in the
compositor, in ishwl and in the iOS app.

## Frame loop

```
capture_output(overlay_cursor=1)
  → buffer(format,w,h,stride)   allocate/reuse a shared buffer of exactly that size
  → copy_with_damage(buffer)    one frame in flight, self-pacing
  → damage(x,y,w,h)*            unioned into one rectangle
  → ready                       attach + damage_buffer + commit on the ishwl surface
  → wl_surface.frame callback   the next capture starts here
```

Every presented frame is appended to `/tmp/omarchy-pad/frames.log` as

```
MS SEQ W H DAMAGE_W DAMAGE_H
```

A frame that fails (`failed`, an output that cannot render, a size the compositor
refuses) is logged, counted in `capture_failures` and retried after 500 ms.  If the
compositor produces nothing at all, the bridge still commits a dark placeholder of the
target size so the iPad window exists and the app's watchdog sees a live bridge.

**Resize.** `xdg_toplevel.configure(w,h)` from ishwl (the iPad window's size) becomes
`keyword monitor HEADLESS-1, <w>x<h>@60, 0x0, 1` on Hyprland's **IPC socket**
(`$XDG_RUNTIME_DIR/hypr/<instance>/.socket.sock`, the request `hyprctl` sends; the
fork/exec of `hyprctl` is only the fallback — hyprctl cannot always set its socket
timeouts under iSH).  The new `buffer` event from screencopy reallocates the shared
buffers.  Every buffer is a new file, so a mapping in the app can never SIGBUS on a
shrink.

`monitor = HEADLESS-1, …` in the config is only a rule: it does not create the output.
If no output with the wanted name exists after connecting, the bridge asks for one over
the same IPC socket (`output create headless HEADLESS-1`) and captures it as soon as it
appears.

**Input.** ishwl's surface-local coordinates are the output's coordinates (scale 1, one
Linux pixel is one iPad point), so pointer events are forwarded 1:1:
`motion_absolute(time,x,y,W,H)`, `button`, `axis` + `axis_source`/`axis_stop`, each
followed by `frame()`.  Keyboard: the XKB keymap ishwl compiled for the iPad's layout is
copied byte for byte into a file of the bridge's own and handed to
`zwp_virtual_keyboard_v1.keymap`; key press/release go through as the evdev codes the
iPad sent, so modifiers behave as on a PC.  Only counts (not every event) reach the log.

## Status file

`/tmp/omarchy-pad/status`, one `key=value` per line, rewritten atomically (`.tmp` +
`rename`) at least once a second — the iOS app's watchdog reads it exactly like LinPad
Steam's `steam-bigpicture-status`:

```
bridge=up            down only when the bridge could not start
pid=1234
output=HEADLESS-1
size=1280x800        the compositor's output size (what is being captured)
surface=1280x800     the size of the buffer currently attached to ishwl
frames=412
last_frame_ms=…      monotonic ms of the last presented frame
last_frame_age_ms=…  -1 when no frame has been presented yet
seq=412
capture_failures=0
input_motion=… input_button=… input_axis=… input_key=… input_keymaps=…
error=-              a one-line description of the last problem
```

`/tmp/omarchy-pad/bridge.log` holds the same lines with a monotonic timestamp prefix,
`/tmp/omarchy-pad/hyprctl.log` the output of every `hyprctl` the bridge runs.

## Build

Inside the aarch64 colima VM, through the shared builder (also builds `ishwl`):

```sh
sh installation/omarchy/build-guest-tools.sh          # → work/omarchy-pad/guest-tools/arch
```

or directly, anywhere with `wayland-client` and `wayland-protocols`:

```sh
cd omarchy-pad/bridge && make -j4 && make install DESTDIR=/path/to/rootfs
```

The `wlr-*` protocol XMLs are vendored in `protocols/` (from Hyprland's `protocols/`,
licence headers kept); `xdg-shell` comes from `wayland-protocols`.  Linkage is
`libwayland-client`, `libm`, `libc` only.

## Run

```sh
# one headless output for Hyprland, then the bridge
omarchy-pad-session --minimal      # starts Hyprland, then omarchy-pad-bridge
```

By hand, in a session where Hyprland and ishwl are already running:

```sh
export XDG_RUNTIME_DIR=/run/user/0
WAYLAND_DISPLAY=wayland-0 omarchy-pad-bridge -v
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--hypr-socket NAME\|PATH` | `$XDG_RUNTIME_DIR/wayland-*`, skipping ishwl's own socket | Hyprland's Wayland socket (a candidate is accepted only if it offers screencopy and the virtual devices) |
| `--ishwl-socket NAME\|PATH` | `$WAYLAND_DISPLAY`, then `wayland-0` | ishwl's Wayland socket |
| `--output NAME` | `HEADLESS-1` | the `wl_output` to capture |
| `--runtime-dir DIR` | `/tmp/omarchy-pad` | status, logs, shm directory |
| `--shm-dir DIR` | `<runtime-dir>/shm` | shared frame files |
| `--max-fps N` | 60 | cap on the capture rate |
| `--no-resize` | off | never reconfigure the Hyprland output |
| `-v` | | verbose (`-vv` for per-frame lines) |

The bridge is started by `omarchy-pad-session`, which inherits `WAYLAND_DISPLAY` from
ishwl; the Hyprland socket is discovered by connecting.  If Hyprland is not up yet (or is
restarted), the bridge keeps the ishwl window alive, retries every 2 s and reports
`bridge=up` with an `error=` line in `status` — the app's watchdog contract.

## Testing without (or with) Hyprland

Two tools in `tools/` build with `make mock-compositor hypr-ipc` (they are not part of
`make all` and are not installed):

| Tool | What it is |
| --- | --- |
| `mock-compositor.c` | a compositor-side stand-in: `wl_output` (HEADLESS-1), `wl_shm`, `zwlr_screencopy_manager_v1` v2, `zwlr_virtual_pointer_manager_v1` v2, `zwp_virtual_keyboard_manager_v1`, plus a hyprctl-shaped IPC socket. It writes a test pattern into the screencopy buffer with the same direct write Hyprland does, and logs every request it receives, so the whole bridge can be exercised in seconds on a host with no compositor. |
| `hypr-ipc.c` | one Hyprland IPC request, printed (`hypr-ipc cursorpos`, `hypr-ipc monitors`). Stands in for `hyprctl`, which cannot set its socket timeouts under iSH. |
| `container-test.sh` | the fast loop: bridge + ishwl + mock compositor inside the aarch64 build container (no emulator). |

Gate-0.6 evidence, including the runs against the patched Hyprland in the guest:
`work/omarchy-pad/evidence/gate-0.6/` (`NOTES.md`, per-run logs, the guest's `status`,
`frames.log` and the ishwl view buffers).  The guest driver is
`evidence/gate-0.6/guest/run.sh`.

## Not implemented yet

- **Text input** (`zwp_input_method_manager_v2` / `zwp_text_input_v3`): the on-screen
  keyboard arrives as key events today, so `text` replay, preedit, surrounding text and
  the iPad's input-method switches are not forwarded.  `protocols/input-method-unstable-v2.xml`
  is vendored for when they are.
- **Clipboard and the iPad's text selection** are not bridged to Hyprland's clients.
- **HiDPI**: the ishwl surface is committed at buffer scale 1 and the Hyprland output is
  reconfigured at scale 1, so one logical pixel is one point; the iPad scales the window.
  A scale-2 path (Retina buffer + `wl_surface.set_buffer_scale`) is not written.
- **Multiple outputs**: only `HEADLESS-1` (or the first output) is captured; mirroring
  more of Hyprland's layout is not supported.
- **Clipboard, drag and drop, popups**: ishwl's data-device and popup layers are not used
  by the bridge; the whole desktop is one opaque surface.
- **Rotation / transform**: `wl_output.geometry` transform is ignored (the headless
  output has none).
- **A lost Hyprland connection** is fatal (the bridge exits and the app's watchdog sees
  `bridge=down`); there is no reconnect loop.
