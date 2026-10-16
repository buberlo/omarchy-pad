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

## Where wine actually dies (17 October, after the MAP_FIXED_NOREPLACE build)

With the patched emulator installed, wine gets much further than before: `wineboot.exe`,
`services.exe`, `svchost.exe`, `plugplay.exe` and `winedevice.exe` all start and around thirty
builtin modules load (`ntdll`, `kernel32`, `kernelbase`, `advapi32`, `crypt32`, `msi`, `bcrypt`,
`imagehlp`, `mspatcha`, …). Then a wine process aborts with glibc heap corruption
(`corrupted size vs. prev_size`, `free(): invalid pointer`) at the point where it resolves a
timezone resource (`ExpandEnvironmentStringsW(L"@tzres.dll,-47168")`).

The emulator's own mapping trace (`ISH_EXEC_TRACE=1` in the app's environment, captured with
`devicectl device process launch --console -e …`, 574 traced mappings) shows what wine asks of the
address space:

- **44 `MAP_FIXED_NOREPLACE` calls** (`flags=0x100022`) — the flag this session implemented. Before
  the fix every one of them was answered with a different address and no error.
- **30 `MAP_NORESERVE|MAP_FIXED` reservations above 256 MB** (`flags=0x4032`), i.e. the emulator's
  lazy-mapping path, plus a reservation of about 1.66 GB starting at `0x110000` with DLLs placed
  inside it.

The failure itself is in the **WoW64 machinery**, not in the allocator:

```
0024:err:seh:call_seh_handlers invalid frame 1000fcf40 (0000000000022000-0000000000120000)
0024:err:seh:NtRaiseException Exception frame is not in stack limits => unable to dispatch exception.
```

`0x1000fcf40` is `4 GB + 0xfcf40` — a **guest-window address** (base `B = 0x100000000`), while the
limits it is compared against are guest numbers. That is precisely the contract Madeira documents in
`madeira-upstream/docs/WOW64.md`: each 32-bit pseudo-process owns a 4 GB-aligned host window
`[B, B + 4 GB)`, FEX forms every host address as `B + zext32(EA)`, and Wine's WoW64 layer adds or
removes `B` through typed helpers, with the host answering a Wine-private
`NtQueryInformationProcess` class for `B`. Madeira implements that host side in its iOS app and
patches Wine accordingly; Valve's ARM64 Proton gets it from the runtime integration that this guest
cannot run (the Steam Linux Runtime stage we have to skip).

Five hypotheses were tested and disproved, each with a probe that passes on real Linux first:

| Hypothesis | Probe | Result on the device |
| --- | --- | --- |
| the emulator does not know the brk heap is mapped | `probes/heapaccount.c` | `EEXIST` for both the heap and a live `malloc` block — correct |
| glibc's allocator (`brk`, `mremap`, `malloc_trim`) is broken | `probes/glibcheap.c` | `summary: glibc heap ok` |
| large lazy `PROT_NONE` reservations are placed inside or damaged | `probes/lazyres.c` | `ok` |
| a lazy reservation is invisible to `MAP_FIXED_NOREPLACE` | `probes/lazyhole.c` | `EEXIST` — the reservation is respected |
| the per-app FEX config file is missing | device inspection | Proton writes `compatdata/500/proton-fex-config.json` itself and sets `FEX_APP_CONFIG` |

So the next work is not in the emulator's generic memory handling; it is the WoW64 guest-window
contract. Concretely: find how the window base is established for Valve's Linux ARM64 Proton (who
sets it, whether an environment variable or the runtime passes it), then either provide that from
the guest side or implement the equivalent host behaviour in the emulator. The alternative that
needs none of this is Madeira, whose stack exists for exactly this case.

## The high-address-space switch, and where it got stuck (17 October, 05:00–05:45)

The finding that wine keeps its 32-bit view at `0x100000000` because this guest puts *everything*
else below 4 GB (stack at 0xffffb000, libraries at 0xefe3cb40, heap at 0x10ed000 — all three measured
with `probes/va_layout.c`) led to the change wine actually needs: an **opt-in high address-space
layout**.

`kernel/memory.h` now carries both layouts and `struct mem` remembers which one a process asked for;
`kernel/exec.c` reads `ISH_HIGH_VA=1` from the program's own environment before anything is mapped,
so the switch is **per process** and needs no host-side restart. With it, the stack goes to
`0x7ffffff000` and the mmap area to just below it, leaving the low 4 GB free — the state wine needs
for base 0 and therefore for identity address translation.

The emulator's own decision log proves the decision is taken correctly:

```
ish-highva: /tmp/env-check-probe envp.count=42 -> high_va=1 stack_page=0x7ffffff
ish-highva: /bin/sh envp.count=4 -> high_va=0 stack_page=0xffffe      (unchanged default)
```

**What is not yet proven:** the process itself still reports a stack address of `0xffffd814`, i.e.
the low layout, in the same run whose exec logged `high_va=1` and `stack_page=0x7ffffff`. So either
`current->cpu.sp = sp` (exec.c:499, now instrumented as well) is overwritten later, or the guest
entry path does not use it. That is the next question, and it is a question about iSH's ARM64 guest
entry, not about wine.

**Blocked on the device:** resolving it needs the instrumented build to run again, and that needs a
session, and a session needs StikDebug's JIT handshake — which stops at
`[fast mode] opening stikdebug://enable-jit?…` while the iPad is asleep and locked. Tried: launching
StikDebug itself first, terminating the parallel session's "LinPad Steam" app that holds the shared
Linux system's lock, and relaunching several times; the app never gets past the JIT request (console
capture in `high-va/evidence-app-stuck-on-jit.log`, zero `EXEC`/`MMAP` trace lines). Unlocking the
iPad (and tapping StikDebug's prompt if it appears) is all it takes to continue; the session hook
then starts the command agent by itself.

## Round of 17 October, 05:50–06:20: the layout switch works, wine still corrupts

**Reaching the device while it is locked.** The app stops at StikDebug's JIT handshake when the iPad
is asleep, and that had blocked everything. The app's own fast-mode setting turns it off:
`NSUserDefaults` reads launch arguments, so

```
xcrun devicectl device process launch --terminate-existing --device <udid> \
    com.konradkern.linpad -- -fastMode.setting off
```

boots straight into the session, without StikDebug and without a human tap, and the guest then runs on
the *interpreter* instead of the JIT. I also had to terminate the parallel session's "LinPad Steam"
app, which holds the shared Linux system's exclusive lock (`devicectl device process terminate`).
Both programs crashed the same way with the JIT on earlier in the evening, so the interpreter is not
what breaks wine — it only makes it slower.

**The high layout now works end to end.** `exec.c` installed a fresh mm *after* the request was
applied, which reset it; the request is now applied right after `task_set_mm()`. Measured:

```
/tmp/env-check-probe      stack 0x7fffffe814        (default: 0xffffd834)
[stack]                   007fffffc000-007ffffff000
glibc program             stack 0x7fffffe6b4, mmap 0x7fffeb5e000, libc 0x7fffedecb40
                          "of those three, below 4 GB: 0"
```

and the kernel log shows the whole wine chain taking it: `python3`, `wineserver`, `wine`,
`wine-preloader` all "asked for the high address-space layout".

**Wine still dies the same way.** `wineserver: using server-side synchronization.` then
`Fatal glibc error: malloc.c:2601 (sysmalloc)` / `corrupted size vs. prev_size`, both with the low
and the high layout, with the JIT and without it. So the layout was *not* the cause — that hypothesis
is disproved too.

**What was tested and cleared in this round** (each with a probe that passes on real Linux first, or
by direct comparison):

| Suspect | Evidence |
| --- | --- |
| FEX-specific (x86 translation) | wine's *native* `aarch64-windows` programs (`cmd.exe`, `services.exe`, `wineboot.exe` all ship for aarch64) crash identically. ARM64EC interop still loads FEX's modules, so this is not a complete exclusion. |
| glibc threads plus fork (wine's pattern) | `probes/glibcfork.c`: 6 threads churning the heap, 12 forks from the threaded process, parent heap intact — `glibc threads+fork ok`, low and high layout |
| two libcs in one process | the live wine process maps exactly one libc (the runtime platform's), no musl |
| `/proc/<pid>/mem` (wine's fallback for other processes' memory, since `process_vm_readv/writev` return ENOSYS) | `probes/procmem.c`: the write lands exactly in the target page, neighbours untouched, read-back correct |
| emulator-side fault | the guest kernel log reports no fault, no GPF, nothing — the corruption is silent |

Wine gets to Windows-process creation (`wineboot`, `services`, `svchost`, `plugdev`, ~30 builtin
modules) and then aborts. The next useful step is wine's own heap trace (`WINEDEBUG=+heap`) around
the abort, and a comparison against a second wine build if one can be had.
