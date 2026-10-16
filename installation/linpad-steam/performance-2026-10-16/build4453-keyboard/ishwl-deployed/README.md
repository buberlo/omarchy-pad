# The compositor fix is on the device, and the key log proves it

The prepared `wl-bridge` change (`bd6e9881`) was installed on the physical iPad on
16 October 2026 and the desktop session was restarted onto it. Unlike build 4453, this step is
backed by the compositor's own log, not only by a report.

## What was installed

| | |
| --- | --- |
| Binary | `ishwl` built from `bd6e9881` (aarch64, Alpine 3.21) |
| Digest | `a6ed7453e2ceb0ceb7e21603489244ae0d9a0888a21921fce3c6ea2802bf7f44`, 162 856 bytes |
| Replaced | `d47335e07795a2a9bb378ae7f4ee4a453bc06629f9e0c67aff257fa9bf1bcb13`, the compositor from the Linux system (rootfs 202610031701) |
| Kept as | `/tmp/ishwl-before-kb4453` on the device (the rollback) |
| Also installed | `/etc/ishwl/session.d/99-kb4453-frames.sh` → `ISHWL_FRAMELOG=/tmp/hollow4452-frames.txt`, the frame log the archived measurement helper reads |
| Verified before/after | [reports/kb4453-round1.txt](reports/kb4453-round1.txt) (sha256sum -c OK, `RESULT=INSTALLED`, digests as above) |

Guest packages the binary links against, read from the device:
musl 1.2.6-r4, libxkbcommon 1.7.0-r1, wayland-libs-server 1.23.1-r0, zlib 1.3.2-r0 — the same
set the build container used.

## What the key log shows

`/tmp/ishwl.log` after the restart, over one play session (17:48–17:50 UTC), collected by
[reports/agent-07-evidence.txt](reports/agent-07-evidence.txt):

| evdev | key (position) | presses | why it matters |
| --- | --- | --- | --- |
| 45 | X | 116 | a printable key: before this change it never reached the app as a key |
| 44 | Z | 30 | printable |
| 21 | Y | 14 | printable |
| 57 | Space | 44 | printable |
| 28 | Enter | 18 | worked before, still works |
| 106 | Right | 44 | key code before and after |
| 105 | Left | 13 | key code before and after |
| 103 | Up | 4 | key code before and after |

142 presses and 141 releases, and the *first* line of the log is
`key 28 down` at 17:48:02 with `key 45 up` at 17:49:30 as the last: the whole session is
covered, and printable keys are in it as key events. On build 4453 with the system compositor
the same keys produced no log line at all, because the host sent them as committed text
(`ime`) and the game ignores it; only Enter, Tab, Backspace and the arrows arrived as keys.
That is the defect this deployment removes.

The compositor also reports `text input active in view 1` for the game window, and
`map toplevel 1 'Hollow Knight' hollow_knight.x86_64` — the app id `LinuxTextInputMode` lists.

## Numbers from the same session

- **Frame deliveries of the game view**: 1 026 in a 30 s window during play (34.2/s), and
  6 839 rows over the whole 140.7 s session (48.6/s), measured with the archived
  `hollow4452-frame-capture` helper ([reports/agent-04-perf.txt](reports/agent-04-perf.txt)).
  These are *composited view deliveries*, not an FPS measurement
  (`GAMEPLAY_FPS_CLAIM=0` in the helper's own report). The archived 4451 menu baseline was
  212 deliveries in 30 s (7.07/s) at 659×540; different revision, different scene, different
  window size — not a like-for-like comparison, and it is not presented as one.
- **Window**: `Desktop is 1180 x 780 @ 60 Hz`, `InitializeOrResetSwapChain 1366x950`,
  Vulkan renderer `Virtio-GPU Venus (Apple M2 GPU)`, driver `venus (22), Mesa 26.2.4-1`.
- **Audio**: the Pulse probe found a live stream for the game's pid in two samples 5 s apart —
  `GAME_STREAM index=1 sink=0 pid=7580 corked=no mute=no buffer_usec=64000`. The stream exists,
  is not corked and not muted. That is not the same as "a human heard it".
- **Memory**: app footprint 2 986 MB of the 8 192 MB allowance, headroom 5 205 MB, `kills 0`.
- **Game process**: `FEX-apple-m1-mun` pid 7580, 43 threads.

## What is *not* verified here

- No human has confirmed that the knight moves, jumps, attacks or is audible with this
  compositor, and no per-key result was recorded for the deployment round itself.
- The text-field ordering the change deliberately preserves (only Return, Tab and Backspace
  keep waiting for the app) has **not** been exercised: nobody typed into a text field after
  the restart.
- The 250 ms figure is arithmetic from `ORDER_TIMEOUT_MS`, not a measurement; this session
  shows key events arriving, not how quickly.
- The Steam route is untouched — the game was started with `sh /tmp/hollow4452-egl` by the
  command agent, exactly as by hand.

## Rollback

`agent-commands/4-99-rollback-ishwl.sh` restores `/tmp/ishwl-before-kb4453` over
`/usr/local/bin/ishwl` (with `mv`, since overwriting a running executable fails) and prints the
digests; a desktop session restart then makes it live again. The backup is still on the device.

## How the device was driven

The user ran one bootstrap line in the guest's terminal, which installed the binary, the frame
hook and `mac-side/kb-agent`: a bounded shell loop (180 minutes) that asks the Mac for one
command at a time and posts each result. Every command that ran is in
[agent-commands/](agent-commands/), every answer in [reports/](reports/), and the Mac-side
tools that queued them are in [mac-side/](mac-side/) (`collector.py` serves the files and keeps
one report file per name; `agent-run` queues a command and waits for its report). The agent
does not autostart, survives a desktop session restart (the app's stop script only kills
ishwl, dbus-daemon and pulseaudio) and is removable with
`kill "$(cat /tmp/kb-agent.pid)"; rm -f /usr/local/bin/kb-agent`.
