# Left 4 Dead: what blocks it, and how far the chain gets

Left 4 Dead (appid 500, 7.16 GB) is installed and Steam is configured to run it. It does not start
yet. This record is the diagnosis so far, in the order the blockers appeared, with what is verified
and what is not.

## What the device has

| Piece | State |
| --- | --- |
| Left 4 Dead | installed, `left4dead.exe` plus 32-bit PE `bin/*.dll` (Source 2008) |
| Proton Experimental | installed (`proton`, a python3 script) |
| **Proton 11.0-2 (ARM64)** | installed, appid 4628740, tool name `proton_11-arm64`, bundles `files/share/fex-emu` |
| Steam Linux Runtime 4.0 | installed (x86_64), appid 4183110 |
| Steam Linux Runtime 4.0 for arm64 | installed, appid 4185400 |
| FEX (Steam package) | `steamapps/common/FEX-Emu/`, appid 3127680 |
| The device's own FEX | `/opt/fex/FEX-apple-m1-munmap` plus the EGL hook `/usr/local/bin/linpad-egl-hook` — the route Hollow Knight uses |

## Blockers found, in order

1. **No `python3` in the guest.** Both `fex-compat-tool` and `proton` are `#!/usr/bin/env python3`
   scripts. Every launch died instantly with `env: can't execute 'python3': No such file or
   directory`. Fixed with `apk add python3` (3.12.15).
2. **No `/usr/bin/printenv`.** `fex-compat-tool` calls `subprocess.call(['/usr/bin/printenv'])` for
   debug output; the guest has no such file, and the unhandled `FileNotFoundError` killed the tool
   before it did anything. `apk add coreutils` provides `printenv`, but at `/bin/printenv`
   (this filesystem has no merged `/usr`), so `/usr/bin/printenv` was added as a symlink to it.
3. **No guest rootfs for the x86_64 FEX route.** With the tool running, FEX came up, `/tmp/fex-compat-tool-*.log`
   showed `graphics_provider '/usr/share/guestos/fex-mesa/graphics_provider.json' didn't provide
   graphics_provider.json`, and the FEX config it wrote contains
   `"RootFS": "/usr/share/guestos/fex-mesa/"` — a path that does not exist on this iPad. On
   SteamOS-style ARM hosts that directory is an OS package with FEX, the x86_64 guest libs and
   Mesa. The FEX process then sat at 0 CPU ticks with only `FEX` and `FEXServer` alive.
4. **The mapping sent a Windows game through the x86_64 route.** `config.vdf` had no entry for
   appid 500, so Steam used its default (`Proton Experimental` through `fex-compat-tool` +
   `SteamLinuxRuntime_4`) — the chain that needs the missing rootfs. It is now mapped to
   `proton_11-arm64`, verified in the compat log:
   `Mapping AppID 500 to tool "proton_11-arm64" with priority 250`
   (`scripts/kb4453-steam-mapping`, backup `config.vdf.kb4453-mapping-backup`). Hollow Knight's
   `linpad-fex-native` mapping is untouched.
5. **The Steam client no longer finishes starting.** After several client restarts the client
   stalls in CEF initialisation (`Created mapping SteamChrome_MasterStream_uid0_spid…_mem when set
   to fail if created` every 10 s, zero `steamwebhelper` processes, and eventually no
   `Console Log Start` at all), so `steam -applaunch 500` is forwarded but never acted on — no
   new `LaunchApp` line, no process, no `pfx` directory. `-vgui` and a hard restart of the client
   did not help. Nothing in the guest looks exhausted by the numbers we can read (footprint
   1.7 GB of 8.2 GB, `kills 0`); this needs a fresh emulator, i.e. closing and reopening LinPad,
   which also removes this session's command agent.

Those relocation warnings in the client log (`libcrypto.so.3: __isoc23_strtol: symbol not found`,
`libz.so.1: __snprintf_chk: symbol not found`) are a musl loader resolving glibc libraries. They
appear both before and after the client broke, so they are not established as the cause.

## What is verified

- The game is installed and its binaries are 32-bit Windows PE.
- Proton 11.0-2 (ARM64) is registered with Steam as `proton_11-arm64`, depends on
  `steamlinuxruntime_steamrt4-arm64` (installed), and its command prefix is
  `SteamLinuxRuntime_4-arm64/_v2-entry-point --verb=waitforexitandrun -- 'Proton 11.0 (ARM64)'/proton waitforexitandrun`.
- The mapping for appid 500 is set and survives a client restart.
- The x86_64 route really does reach FEX: FEX and FEXServer started, wrote
  `compatdata/500/fex-emu/{Config.json,AppConfig,Server}`, and stopped at the missing rootfs.
- The Steam client was working earlier the same day (Hollow Knight launched through it at 18:15),
  so the client stall is a regression from the churn, not the starting state.

## What is *not* verified, and the risks

- **Nobody has seen L4D run.** Not a menu, not a window, not a log line from the game itself.
- **L4D is 32-bit.** Proton ARM64 runs x86 Windows code through FEX's wow64 path; whether that
  path covers a 32-bit Source engine title is unknown here. The x86_64 route (Proton Experimental
  under the host FEX) has the opposite problem: it needs a guest x86_64 rootfs the device lacks,
  although `/opt/hollow-runtime`, `/opt/hollow-mesa26` and `/opt/hollow-egl` exist and are what
  the Hollow Knight route uses — making `/usr/share/guestos/fex-mesa` a symlink or bind of those
  is the obvious next experiment on that route.
- Two more emulator gaps are in play for anything Proton-shaped: Steam's `reaper` cannot run here
  (already replaced by a stub for the Hollow Knight route) and `Fossilize ERROR: Failed to set
  PR_SET_PDEATHSIG, aborting child startup` appears in the launch chain logs.

## Next steps, in order

1. Close LinPad from the app switcher and reopen it (StikDebug's JIT approval if it asks) — that
   gives a fresh emulator and a client that can start CEF again.
2. Start Steam from the desktop and press Play on Left 4 Dead. The mapping is already
   `proton_11-arm64`, so this is the route that will be tried; the first run has to create the
   Wine prefix, which is slow under emulation — allow several minutes and watch
   `compatdata/500/pfx`, `ps` for wine/proton processes and `logs/console_log.txt`.
3. If Proton ARM64 cannot handle the 32-bit game, try the x86_64 route instead: point
   `STEAM_COMPAT_GRAPHICS_PROVIDER` (or the FEX `RootFS`) at the device's own x86_64 stack, i.e.
   make `/usr/share/guestos/fex-mesa` exist and describe `/opt/hollow-runtime` + Mesa, and set the
   mapping back to the FEX tool.
4. The command agent from this session is gone after the restart; the one-line bootstrap in
   `../build4453-keyboard/ishwl-deployed/README.md` brings it back for scripted work.

## Files here

- `reports/` — the agent reports behind every claim above, in order (`agent-29…51`).
- `scripts/kb4453-steam-mapping` — the tested VDF editor for `CompatToolMapping` (11 local checks:
  add, replace, idempotent, refuse without the block, restore on mismatch).
- `scripts/kb4453-steam-launchopts` — the earlier tested editor for an app's launch options.
- `scripts/linpad-hk-fex` — the Hollow Knight Steam wrapper, kept here because the L4D work touched
  the same Steam chain.
