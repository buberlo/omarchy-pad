# Omarchy Pad

An iPad app that boots **Arch Linux ARM + [Omarchy](https://github.com/basecamp/omarchy)** locally
and shows the real Hyprland session edge to edge — no VM, no jailbreak, no remote machine. The Linux
system is a userspace emulator ([iSH-ARM64](https://github.com/ish-app/ish)) running inside the app;
the desktop's pixels and the iPad's keyboard, trackpad and touch go through a Wayland bridge.

This repository is a fork of [LinPad](https://github.com/fspecii/LinPad) (itself a fork of iSH) that
adds the Omarchy variant. The work lives on the `omarchy-pad` branch; `main` is the upstream LinPad
archive branch.

## What works today, honestly

| Gate | What it proves | State |
|---|---|---|
| 0.1 / 0.1b | Arch Linux ARM boots in the emulator (CLI and in the app) | pass |
| 0.2 | The Omarchy image boots and its tooling runs | pass |
| 0.3 | A GPU: virtio-gpu render node, GLES 3.0 through zink on Venus | pass |
| 0.4 | Hyprland renders headless with Aquamarine, patched for a render-node-only guest | pass |
| 0.5 | The session layer: supervisor, buses, pipewire, session script | pass |
| 0.6 | The display/input bridge into an ishwl surface | pass |
| 0.7 | iPad text input / IME into Linux apps | not started |
| 0.8 | The app on a device: session on screen at usable frame rate | in progress |

Two things are worth saying plainly. The desktop has been brought up **in the emulator and in the
CLI guest**, and the bridge's launch channel is verified in a container; the app has **not** yet shown
the finished session on an iPad or in the Simulator — the last run stopped at a boot-time descriptor
leak that is fixed in `574d2e96` but not re-measured since. And the frame rate of a real Hyprland
session is around one frame per second at 1280×800 under the interpreter (the drawn-by-hand mock
path is ~40 fps), so performance is the next real problem after bring-up.

## Where things are

- **`docs/omarchy/GATES.md`** — the gate log: what each gate proved, the emulator changes it needed,
  and the open `execve` bug (a forked guest process cannot `execve` at all in an app guest, which is
  why launches go through PID 1).
- **`installation/omarchy/`** — the guest image (`build-image.sh`: Arch Linux ARM plus an Omarchy
  4.0.0-alpha runtime, ~900 MB compressed), the Hyprland/Aquamarine patches built for a
  render-node-only guest, the container check for the launch channel, and the app build scripts.
- **`omarchy-pad/files/`** — what the guest runs: `omarchy-pad-init` (PID 1: systemd cannot run under
  this emulator), `omarchy-pad-session` (Hyprland plus the bridge), the shim package that answers
  `systemctl` and friends.
- **`omarchy-pad/bridge/`** — the display and input bridge: Hyprland's headless output copied through
  `wlr-screencopy` into a shared buffer, input back through the virtual-pointer and virtual-keyboard
  protocols.
- **`desktop/DesktopKit/`** — the Swift side: the `omarchy` variant and its one-surface canvas, and
  the host half of the bridge.
- **`wl-bridge/`** — ishwl, the in-guest compositor that hands toplevels to the iPad as native
  windows, now with the supervisor launch channel.

## Building

```sh
# the guest image (Docker; writes work/omarchy-pad/omarchy-aarch64-pad.tar.gz, ~900 MB)
sh installation/omarchy/build-image.sh

# the app: for a device, or for the Simulator
sh installation/omarchy/build-local.sh
sh installation/omarchy/build-sim.sh
sh installation/omarchy/run-sim.sh <simulator-udid>

# the supervisor/ishwl launch contract, without an emulator
sh installation/omarchy/launch-channel-test/run.sh
```

`work/` holds every generated artifact (images, guest tools, staged apps, gate evidence) and is not
tracked.

## License and attribution

GPLv3, like the projects it is built on: [iSH](https://github.com/ish-app/ish) by Theodore Dubois and
contributors, and [LinPad](https://github.com/fspecii/LinPad) by Konrad Kern. See `LICENSE.md` and the
additional iOS terms in `LICENSE.IOS`; the changes here are released under the same terms.

"Omarchy" is the name of the upstream Linux distribution this app runs; this project is not affiliated
with or endorsed by Basecamp or the Omarchy maintainers, and "Omarchy Pad" is a working title that
will change before anything ships.
