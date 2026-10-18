# omarchy-pad — the guest side of Omarchy Pad

Everything in `files/` is installed into `/` of the Arch Linux ARM + Omarchy image by
`installation/omarchy/build-image.sh`. The layout mirrors the eventual pacman package so
the same tree can become a PKGBUILD without moving files.

| Path | What it is |
| --- | --- |
| `files/usr/local/bin/omarchy-pad-init` | PID 1: buses, system daemons, audio; then only reaps. `systemd` cannot run under the emulator, so this replaces `/sbin/init`. |
| `files/usr/local/bin/omarchy-pad-session` | Starts Hyprland (Omarchy's config, or `--minimal` for bring-up) and then `omarchy-pad-bridge`. Launched by the app's canvas through ishwl's spawn channel. |
| `files/usr/local/lib/omarchy-pad/shim` | Multi-call shim, symlinked as `systemctl`, `systemd-run`, `systemd-cat`, `uwsm-app`, `journalctl`, `loginctl`, `hostnamectl`, `timedatectl`, `udevadm`, `ufw`, `bluetoothctl`, `rfkill`, `brightnessctl`. |
| `files/usr/share/omarchy-pad/hyprland-pad.conf` | The bring-up config: one headless output, no effects, a terminal to capture. |

Rules this tree follows (borrowed from Try Omarchy's guest package):

- Install only under `/usr`; never edit a file another package owns.
- Everything the session needs is a file here, so the image build is the only installer.
- Anything the emulator cannot provide (seats, udev, KMS, cgroups, systemd) is answered by
  a shim that says so, and every shim call is logged to `/run/omarchy-pad/shims.log` for
  the app's diagnostics.
