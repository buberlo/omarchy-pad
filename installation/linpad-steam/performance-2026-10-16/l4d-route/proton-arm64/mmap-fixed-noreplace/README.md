# The MAP_FIXED_NOREPLACE fix: built, installed, verified on the device — wine still fails

This is what happened after the plan's phase B found the missing flag. The fix is real and it is on
the iPad; it was not enough to get wine running, and three further hypotheses have been tested and
ruled out. This directory holds the build, the probe and the evidence.

## The fix

`kernel/calls.h` gains `MMAP_FIXED_NOREPLACE` (0x100000) and `kernel/mmap.c`'s `do_mmap` now
implements it: without an address or with a misaligned one it is `EINVAL`; when any page of the
requested range is already mapped it is `EEXIST` and nothing changes; otherwise the mapping is placed
exactly where it was asked for (internally it becomes a `MAP_FIXED` placement, so every existing
"exact placement" path applies). The collision check deliberately comes first and is not turned into
`EINVAL` by a simultaneous `MAP_FIXED`, because a collision reports `EEXIST` on Linux even with both
flags set — measured in a Debian container, not assumed.

`tests/manual/steam-compat/map-fixed-noreplace-probe.c` is the acceptance test. It passes on real
Linux (aarch64 Debian: `ok`, exit 0) and now on the iPad.

## The build and the install

| | |
| --- | --- |
| Binary | `work/performance/build-mmapfix/Payload/iSH ARM64.app/iSH ARM64`, sha256 `2534001c…` |
| Unsigned build output | sha256 `5a3ea335…`, `** BUILD SUCCEEDED **` (41 463 752 bytes) |
| Rollback | `work/performance/rollback-installed/Payload/iSH ARM64.app/iSH ARM64`, sha256 `051ed164…`, signed and ready |
| Signed with | `python3 work/sign-linpad.py`, "SIGNED AND VERIFIED … profile expires 2027-10-16" |
| Installed with | `xcrun devicectl device install app --device 00008112-000924EE14D9A01E` |
| Built from | a git worktree of `c3933a9c` (`work/LinPad-mmap-fix`), so the parallel session's uncommitted work was not mixed in |

Two build notes worth keeping: a fresh worktree has no `gpu/prefix-ios`, and the link fails with
`no such file or directory: …/libvirglrenderer.a` until that directory is symlinked from the main
checkout; and the first link attempt also reported `virglrenderer.pc missing`.

On the device, after the install:

```
MAP_FIXED mid: replaced exactly the requested range
MAP_FIXED_NOREPLACE on a live page: refused with EEXIST
MAP_FIXED_NOREPLACE on a free page: placed at 0xefff4000
MAP_FIXED|MAP_FIXED_NOREPLACE on a live page: refused with EEXIST
ok
```

Before the fix the same probe said `got=0xeffee000 errno=0 (unexpected; want MAP_FAILED/EEXIST)`.

The install also confirmed that the guest's own working state survives an app update: `/opt/linpad-glibc`,
`/usr/local/bin/linpad-proton-arm64`, `/usr/share/guestos/fex-mesa`, the probes in `/tmp/l4d-probes`
and the Steam mapping for appid 500 were all still there afterwards. A one-shot
`/etc/ishwl/session.d/99-kb4453-agent.sh` started the command agent again after the restart and
removed itself (`/tmp/kb-agent-boot.log`: `kb4453 agent started pid=92`).

## Wine still dies, and three hypotheses are now ruled out

With the patched emulator, the 32-bit Windows PE through the ARM64 Proton still ends in

```
wineserver: using server-side synchronization.
Fatal glibc error: malloc.c:2601 (sysmalloc): assertion failed: ...
corrupted size vs. prev_size
malloc_consolidate(): invalid chunk size
```

Each of the following was tested with a probe that passes on real Linux first:

1. **Heap accounting** (`probes/heapaccount.c`): does the emulator know the brk heap is mapped? It
   does — `MAP_FIXED_NOREPLACE` inside the heap and over a live `malloc` block both report `EEXIST`
   on the device, exactly as on Debian. (`/proc/self/maps` does not label `[heap]`, which is a
   cosmetic difference, and `sbrk()` growth failed for the musl build.)
2. **glibc's allocator itself** (`probes/glibcheap.c`): 2000 brk-driven allocations, a 64 MB block
   grown to 96 MB across the mmap threshold (glibc's `mremap` path), `malloc_trim` and regrowth —
   `summary: glibc heap ok`, under both the device's glibc 2.41 and the SLR platform's.
3. **Lazy reservations** (`probes/lazyres.c`): a 1 GB `PROT_NONE` reservation (past the emulator's
   128 MB/256 MB lazy thresholds), then a plain `mmap(NULL, 1 MB)`, then a writable touch inside the
   reservation — the anonymous block was placed outside the reservation and stayed intact:
   `ok`. The same held on real Linux.

So the corruption is not the allocator, not the heap bookkeeping and not the reservation visibility.
It is something wine does that the probes do not yet reproduce.

## Where this leaves the investigation, and the next step

The emulator has a mapping trace: `ISH_EXEC_TRACE` in the environment makes `do_mmap` print every
mapping of 2 MB or more (`MMAP: pid=… addr=… len=… prot=… flags=… fd=…`). `devicectl device process
launch --console -e '{"ISH_EXEC_TRACE":"1"}'` attaches the app's own output, so the sequence of
mappings wine performs right before the abort can be captured in full. That attempt was started
(`evidence-app-console-trace.log`) but could not be completed: separating the app from StikDebug's
JIT handshake left the guest session waiting, so no command agent was available to start wine. The
next attempt needs an unlocked, awake iPad.

After the trace, the likely candidates are wine's own `mprotect`/`munmap`/`MADV_*` patterns on its
reservations, and the emulator's demand-mapping path (`pt_map_lazy` plus `mem_find_reservation`) that
serves touches inside lazily reserved regions.

## Files

- `app-digests.txt` — the two signed binaries (the patched build and the ready-to-install rollback).
- `build.log`, `build-first-attempt.log`, `install.json` — the build and install, verbatim.
- `evidence-app-console-trace.log` — the console capture that shows the app asking StikDebug for JIT
  and then waiting.
- `../probes/` — every probe source; `reports/` — the agent reports 71–78.
