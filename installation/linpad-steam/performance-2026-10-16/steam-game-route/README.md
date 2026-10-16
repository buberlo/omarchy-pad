# Steam → Hollow Knight: the Play button now starts the game

The compatibility tool was already selected for appid 367520 and Hollow Knight's launch options
were already set, but every launch from Steam ended in the same second. Two things were wrong,
both found on the device, and both are now fixed and verified:

1. **Steam's own `reaper` cannot run under iSH.** The launch chain Steam builds is
   `steam-launch-wrapper -- reaper SteamLaunch AppId=367520 -- <tool>/run waitforexitandrun -- …`,
   and the reaper dies with `prctl() failed!` before the compatibility tool is ever reached.
   `-noreaper` on the client does not change that (the flag is accepted and the reaper is still
   inserted). The reaper is replaced by a stub that drops the `SteamLaunch AppId=… --` prefix and
   execs the rest, which keeps the wrapper and the tool intact.
2. **The tool's defaults did not match this device.** `launch-game` defaulted to
   `translator=/opt/fex/FEX-apple-m1` and `display_mode=x11`. The route that has been verified on
   this iPad by hand uses the EGL diagnostic hook (`/tmp/fex-hollow-egl` → the patched
   `/opt/fex/FEX-apple-m1-munmap`) and Wayland/Venus. The defaults now point at
   `/usr/local/bin/linpad-egl-hook` and `wayland`; the hook was copied out of `/tmp` to that
   stable path, byte for byte.

## The command Steam builds, and what happened

```
/bin/sh -c /root/.steam/sdkarm64/steam-launch-wrapper -- \
  /root/.local/share/Steam/steamrtarm64/reaper SteamLaunch AppId=367520 -- \
  '/root/.local/share/Steam/compatibilitytools.d/linpad-fex-native'/run waitforexitandrun -- \
  '/root/.local/share/Steam/steamapps/common/Hollow Knight/hollow_knight.x86_64' \
  -timestamps -force-wayland -force-vulkan -screen-width 960 -screen-height 540 \
  -screen-fullscreen 0 -logFile /tmp/hollow4452-egl-unity.txt
```

Before the stub, `Game process added … ProcID 4671` and `Game process removed … ProcID 4671`
appear in the same second for every attempt. After it (18:15:22 UTC):

- `5036 FEX-apple-m1-mu` — the game process, running.
- `map toplevel 4 'Hollow Knight' hollow_knight.x86_64` and `text input active in view 4` in
  `/tmp/ishwl.log`.
- Unity's log (`/tmp/hollow4452-egl-unity.txt`, the file the launch options name) is fresh and
  progressing: language `DE`, `Loading in SingleInstance mode`, physics and asset loading.
- 30 s frame capture for view 4: **1 801 deliveries** with the archived helper
  (`start_line=19845 end_line=21646`), Vulkan renderer `Virtio-GPU Venus (Apple M2 GPU)`,
  Mesa 26.2.4-1, PSO cache loaded from `/tmp/Team Cherry/Hollow Knight/vulkan_pso_cache.bin`.
- A second capture two minutes later, the game idling in its menu with nobody touching it:
  **1 802 deliveries in 30 s (60.1/s)** — the compositor kept up with the 60 Hz display cadence at
  this window size (659×540). The hand-launched session delivered 1 026 in 30 s (34.2/s) at
  1366×950, so the two numbers are not comparable: same metric, different window.
- **Audio through this route**: the Pulse probe found a live stream for the game's pid in two
  samples 5 s apart — `GAME_STREAM index=1 sink=0 pid=5036 corked=no mute=no buffer_usec=64000`.
  The stream exists, is uncorked and unmuted; that is not the same as a human hearing it.
- App footprint 4 663 MB of the 8 192 MB allowance, headroom 3 528 MB.
- Nobody pressed a key in either capture, so `key` lines are 0 — **input through the Steam route
  is not what this record shows**.

## What is not verified

- No human has played the game **from Steam**: movement, combat, menus and audio are unconfirmed
  for this route, exactly as they were for the hand launcher.
- Frame *deliveries* are not an FPS measurement, and the number above is not comparable with the
  hand-launched session's 1 026/30 s: that run had a 1366×950 window, this one 659×540.
- The reaper stub removes process supervision. Steam still sees the game process it spawned, but
  this is a workaround for an emulator gap (`prctl`), not a fix in iSH, and it should be revisited
  if LinPad grows that syscall.
- Steam's own launcher chain still prints `VK_INSTANCE_LAYERS=VK_LAYER_VALVE_rpo:VK_LAYER_VALVE_fdm_injection`;
  the tool unsets it (`launch-game` scrubs Steam's Vulkan layers), and the Venus renderer above is
  what the game actually used.

## What changed on the device

| Path | Before | After | Backup |
| --- | --- | --- | --- |
| `~/.local/share/Steam/compatibilitytools.d/linpad-fex-native/launch-game` | `db885d88…` | `3b2114fd…` | `launch-game.before-kb4453` (`db885d88…`) |
| `~/.local/share/Steam/steamrtarm64/reaper` | `6105162a…` (ELF) | `25a0ae87…` (stub) | `reaper.before-kb4453` (`6105162a…`) |
| `/usr/local/bin/linpad-egl-hook` | — | `0638ad92…` (copy of `/tmp/fex-hollow-egl`) | `/tmp/fex-hollow-egl` still there |
| `~/.local/share/Steam/userdata/38877668/config/localconfig.vdf` | `ca099454…` | `79ff9d34…` | `localconfig.vdf.kb4453-backup` (`ca099454…`) |
| `~/.local/share/Steam/config/config.vdf` | unchanged | unchanged | the mapping `367520 → linpad-fex-native` was already there |

Launch options, set with `kb4453-steam-launchopts` (which verifies its own readback and restores
the backup on any mismatch):

```
-timestamps -force-wayland -force-vulkan -screen-width 960 -screen-height 540 -screen-fullscreen 0 -logFile /tmp/hollow4452-egl-unity.txt
```

Rollback: copy each `*.before-kb4453` / `*.kb4453-backup` back over its file (the `reaper` and
`launch-game` with `mv`, since a running executable cannot be overwritten), and Steam must not be
running while `localconfig.vdf` is replaced.

## Files here

- `artifacts/launch-game`, `artifacts/reaper-stub`, `artifacts/linpad-egl-hook` — the exact files
  the device runs, as fetched from it (digests above).
- `reports/` — the agent reports behind every claim: the Steam config probe, the failure, the
  isolated reaper test, the stub, the successful launch and the measurement.
- `evidence/steam-launch-command.txt` — the command Steam built and the observation lines.
- The tracked sources these came from are in `../steam-compat/` (`linpad-fex-native/launch-game`
  now carries the patched defaults, plus `reaper-stub` and `linpad-egl-hook`).
