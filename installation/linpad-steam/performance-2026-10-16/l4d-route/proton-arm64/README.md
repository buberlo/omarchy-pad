# ARM64 Proton on the iPad: wine starts, the emulator stops it

This is the execution of the plan "Left 4 Dead auf dem iPad zum Laufen bringen": the x86_64 route
with the device's own FEX (phase A) and Valve's ARM64 Proton without the Steam Linux Runtime
(phase B). Both were expected to be plumbing work. Phase A answered the 32-bit question with a
measurement, phase B got wine 11.0 running in the guest and then hit a concrete emulator bug, which
is now reproduced in twelve lines of C.

## Phase A — the x86_64 route with the device's own FEX (as requested)

Setup, all on the device:

- `/usr/share/guestos/fex-mesa` — the guest rootfs that `fex-compat-tool` writes into FEX's config as
  `RootFS` and that this iPad did not have. Composed of symlinks into the device's own x86_64 stack
  (`/opt/hollow-runtime` for `bin`, `etc`, `lib`, `lib64`, `sbin`, `usr`) plus a
  `graphics_provider.json` that records where the Venus Vulkan ICD and the EGL vendor manifest live.
- Four probe binaries built with the workspace's zig (`probes/hello.c`): 32-bit and 64-bit Linux ELF,
  32-bit and 64-bit Windows PE.

Results:

| Probe | Result |
| --- | --- |
| 64-bit Linux ELF via `/opt/fex/FEX-apple-m1-munmap` with `FEX_ROOTFS=/usr/share/guestos/fex-mesa` and `LD_LIBRARY_PATH=/opt/hollow-runtime/usr/lib/x86_64-linux-gnu` | **works**: `l4d-probe: hello from 64-bit linux` |
| 32-bit Linux ELF through the same FEX | **refused**: `Invalid or Unsupported elf file. This is likely due to a misconfigured x86-64 RootFS` |

So the FEX on this device runs 64-bit x86 code with a rootfs we can compose ourselves, and it has
**no 32-bit support at all**. That closes the x86_64 route for Left 4 Dead for a measured reason:
the game's engine is a 32-bit PE, and even if Proton Experimental's x86_64 wine ran under FEX, its
32-bit path needs a 32-bit-capable host CPU, which an ARM64 host does not have.

The route remains useful as a 64-bit capability: with the rootfs in place, FEX + the FEX compat tool
can run 64-bit x86_64 Linux and (through Proton Experimental) 64-bit Windows titles.

## Phase B — ARM64 Proton without the Steam Linux Runtime

Valve's `Proton 11.0 (ARM64)` carries exactly what Madeira built for iOS: `files/bin-arm64/wine`
and `wineserver`, `files/lib/wine/aarch64-unix/{libwow64fex.so,libarm64ecfex.so}` and
`files/lib/wine/aarch64-windows/{wow64.dll,wow64win.dll,xtajit64.dll,libarm64ecfex.dll}` — an
ARM64-native wine whose x86 and x86-64 Windows code is translated by FEX's WoW64 module. That is the
32-bit path, and it is why this route, not the x86_64 one, is the way to Left 4 Dead.

Progress, in order:

1. **The glibc/musl wall is solved.** Running `proton` (a python3 script) directly, without
   `SteamLinuxRuntime_4-arm64/_v2-entry-point`, gets past the runtime stage that used to kill the
   launch. Wine then failed with `libc.so.6: cannot open shared object file`, because wine is a
   glibc ARM64 binary in a musl guest. `/opt/linpad-glibc` — symlinks to ten core glibc libraries,
   no `libz`/`libffi`/`expat` that the musl tools would pick up instead of their own — fixes that
   without breaking anything else: `python3`, `ls` and `ps` still work with it on `LD_LIBRARY_PATH`,
   and **`wine --version` prints `wine-11.0`**.
2. **Proton really does work here.** `Proton: Upgrading prefix from None to 11.0-100` and a prefix
   appears at `compatdata/500/pfx` with `system.reg`, `user.reg`, `dosdevices` and
   `.update-timestamp`. It also logs `Proton: Error while copying … amdxcffx64.dll: No such file or
   directory` during prefix setup; that file is not in the ARM64 depot, and it has not been shown to
   matter.
3. **Then wine dies with heap corruption**, both for a 64-bit and a 32-bit Windows PE and with both
   candidate glibc sources (the device's `/opt/steam-host` and the SLR platform's own libraries):
   `wineserver: using server-side synchronization.` followed by
   `Fatal glibc error: malloc.c:2601 (sysmalloc): assertion failed`, `corrupted size vs. prev_size`.
   `wineserver -f -p` on its own survives, so the crash comes from wine's address-space setup, not
   from wineserver.
4. **The glibc allocator tunables are not the cause**: `glibc.pthread.rseq=0`,
   `glibc.malloc.tcache_count=0` and `MALLOC_CHECK_=0` change nothing.

## The bug underneath, reproduced

Two native probes (`probes/ishrepro.c`, `probes/mapfixed.c`, built for `aarch64-linux-musl` and run
directly on the iPad, no FEX involved):

- **fork and thread heap integrity is fine.** 40 rounds of `malloc` → `fork` → write in both → verify,
  and 8 threads × 200 `malloc`/`memset`/verify rounds: `fork_heap ok`, `thread_heap ok`, twice. The
  emulator does not corrupt a normal process's heap.
- **`MAP_FIXED_NOREPLACE` is not implemented.**
  ```
  base=0xeffef000 page=4096
  MAP_FIXED_NOREPLACE on a live page: got=0xeffee000 errno=0 (unexpected; want MAP_FAILED/EEXIST)
  ```
  Linux must fail that call with `EEXIST` and change nothing; this emulator silently treats it as a
  hint and returns a *different* address. `MAP_FIXED` itself is fine (it replaced exactly the
  requested two pages: "neighbour bytes changed by MAP_FIXED: 0"), and a `MAP_FIXED` request at 64 GB
  is honoured.

That is enough to explain the wine crash: wine reserves its Windows address space with
`MAP_FIXED_NOREPLACE`-style probes, is told "yes, you have that range" for ranges it does not own,
and later writes over the host heap — which is exactly what glibc's malloc assertions then report.

The emulator sources have no trace of the flag: it appears in `tests/manual/steam-compat/proc-maps-boundary-probe.c`
and in archived snapshots, never in `kernel/mmap.c`, whose `do_mmap` only ever tests `MMAP_FIXED`
(lines 137, 150, 156, 163 and the anonymous path at 214). The fix is small and well defined there:
`MAP_FIXED_NOREPLACE` (0x100000) must require a non-zero address, must be mutually exclusive with
`MAP_FIXED`, must place the mapping exactly where asked when the range is free, and must return
`EEXIST` — changing nothing — when it is not.

## What this means for Left 4 Dead

- On this device, with the current emulator: **not reachable through LinPad's Linux and Steam.** Not
  because of Steam, Proton or the configuration — those are now demonstrably working up to wine — but
  because wine cannot build its address space on an emulator that answers `MAP_FIXED_NOREPLACE`
  wrongly.
- **The next real step is an emulator change**, in `kernel/mmap.c`, with the reproducer above as the
  acceptance test (`MAP_FIXED_NOREPLACE` on a live page must return `MAP_FAILED`/`EEXIST`). After
  that, the same phase-B command should be repeated: `linpad-proton-arm64` with the 64-bit probe
  first, then the 32-bit one, where the ARM64 wow64 modules are the thing being tested.
- **The alternative that needs no emulator work is Madeira**, which is the same architecture (its
  `docs/WOW64.md` documents ARM64EC wine plus FEX's WoW64 module, and it patches both wine and its
  iOS host for exactly the address-space problem found here). That is a different app with its own
  Steam install, and therefore a fresh download of the game; it is the fastest way to actually play
  Left 4 Dead on this iPad.

## Files

- `linpad-proton-arm64` — the launcher used in phase B (installed at `/usr/local/bin/linpad-proton-arm64`
  on the device). It sets the Steam compat environment, keeps `LD_LIBRARY_PATH` to `/opt/linpad-glibc`
  only, and runs `proton waitforexitandrun` without the Steam Linux Runtime.
- `probes/hello.c` — the four-architecture probe (`zig cc -target {x86_64,x86}-{linux,windows}-gnu`).
- `probes/ishrepro.c` — fork and thread heap integrity, native musl build.
- `probes/mapfixed.c` — the `MAP_FIXED` / `MAP_FIXED_NOREPLACE` semantics probe.
- `reports/` — the ten agent reports behind every number and message above.
