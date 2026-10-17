# Recovery after the guest filesystem was damaged

What happened: while chasing the endless `Error relocating /lib/libcrypto.so.3` flood I removed the
symlinks that put glibc libraries on the musl search path. `/lib` is a symlink to `/usr/lib` in this
image, so that ran through and took musl's own libraries with it. `ls`, `ps`, `tar` and every other
external program stopped starting; the shell survived in memory. The app's own **Repair** cannot fix it
either, because it unpacks its repair kit **inside the guest**:

```
[ Start Repair ]  ->  "Could not unpack the repair kit in Lin…"
```

So the only way back is a fresh rootfs: **delete LinPad on the iPad and install it again**. That loses
the whole Linux side, including Steam, Proton and Left 4 Dead (~1-2 h of downloads). Everything that
does not have to be downloaded comes back from this archive in minutes.

## Order of work after the fresh install

1. **Steam client**: install as before, sign in (the account is `buberlo`). The download of Left 4 Dead
   and the ARM64 Proton runtime is the long part.
2. **Copy the helpers into the guest** (they live in `proton-arm64/` and `steam-client-fix/` here):
   - `/usr/local/bin/steam`, `/usr/local/bin/steam-local` (with the `--argv0` fix and the loader call
     for the client), `/usr/local/bin/ishwl-x11`, `/usr/local/bin/l4d-play`,
     `/usr/local/bin/linpad-proton-arm64` (**without** the global `LD_LIBRARY_PATH` — that one fed the
     glibc directory to a musl python3 and produced the flood),
   - `$HOME/steamwebhelper` as a loader wrapper (the client spawns its UI backend by that path and it
     is a glibc program in a musl guest).
3. **Steam launch chain**: the reaper stub at `$B/steamrtarm64/reaper` and the `_v2-entry-point` stub,
   written **after** the client is up (it restores the originals at startup), plus the platform
   override for app 500 removed from `userdata/*/config/compat.vdf`.
4. **Wine/Proton session**: `/opt/linpad-glibc` with the core glibc libraries, the high-VA emulator
   build (`ISH_HIGH_VA=1`), and the emulator fixes from this session (prctl options, ptrace
   self-tracing, MAP_FIXED_NOREPLACE, memfd sealing, lazy anonymous mappings, unlinked files as shared
   memory objects).
5. **Then**: `l4d-play -g` for the direct Wine route (no Steam DRM handshake) or `l4d-play` for the full
   Steam route.

## What is already proven and does not need rediscovering

- The client no longer asserts at `tier0/threadtools.cpp (2526)` once its crash reporter is reachable at
  all four paths it probes (`696bb0be`).
- The Proton launcher must **not** put the glibc directory on `LD_LIBRARY_PATH` for `python3`; Proton
  sets wine's own paths itself (`41c6b09d`).
- `ishwl-x11` respawns its app and `steam-local` respawns the client, so a launcher that dies
  immediately loops forever and floods the terminal.
- The app writes `Library/Application Support/Diagnostics/{events.jsonl,session.json,emulator-log-snapshot.txt}`
  into its container; all three are readable from the Mac with `devicectl device copy from` and were the
  only way to see inside the guest while the terminal was unusable.
