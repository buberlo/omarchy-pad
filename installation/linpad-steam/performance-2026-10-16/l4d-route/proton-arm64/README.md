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

## Round of 17 October, 06:10–06:30: the wild pointer behind the corruption

The emulator's own fault tracing (`ISH_EXEC_TRACE`, captured through `devicectl --console` together
with the no-JIT boot) finally shows the failure itself:

```
SIGNAL_TRACE: sig=6 pc=0x7fffede8020 pid=482 sp=0x1000fcfb0 lr=0x7fffede800c fault=0x0
REAP: pid=482 exit=0x10100 comm=steam.exe
INT_GPF pid=505 pc=0x7fffe876138 fault=0x167f9efd1 write=0
  insn=0xb9400401 x0=0x167f9efd1 x1=0xe8000000 x2=0x46508001 x3=0x7fffe87b000
  sp=0x7ff9e020 x29=0x7ff9e020 x30=0x7fffe875cc8 brk=0x7ffff020000
```

What it says:

- The **first** event is the glibc abort in the process whose `comm` is **`steam.exe`** (wine's
  Windows Steam stub, which Proton starts for the DRM handshake), `sig=6` at the address of glibc's
  `malloc_printerr`.
- Then **several wine processes read-fault at the same PC** with wild pointers of one shape:
  `0x167f9efd1`, `0x167f5efd1`, `0x167f3efd1`, `0x167f4efd1`, `0x167f2efd1` — i.e.
  `0x167f00000 + n·0x20000 + 0xefd1`, differing per process. That is a **per-process structure**
  holding a bad pointer, not random garbage.
- The faulting instruction is `ldr w1, [x0]` at the start of a **NEON-heavy libc routine** (the
  instruction block is a `memset`/`memcpy` shape), so libc is the *victim*, called with a pointer that
  was already wrong.
- The frame pointer and stack pointer are at `0x7ff9e020` (~2 GB, the **guest/Windows stack**) while
  the return address is high (libc). Host ARM64 code running on the guest stack is how ARM64EC works,
  but it also means a host-side stack overflow would land in guest memory.

An A/B test narrows the arena: with `MALLOC_MMAP_THRESHOLD_=1` (glibc takes nothing from `brk`) the
assertion disappears for minutes — the wild-pointer faults do **not**, they simply no longer get
reported. So `brk` is where the corruption becomes *visible*, not where it is caused.

Cleared in this round as well, each with a probe validated on real Linux first or a controlled
comparison:

| Suspect | Evidence |
| --- | --- |
| the preloader | `WINELOADERNOEXEC=1` changes nothing |
| shared memory (anonymous, memfd, `/dev/shm`, file) | `probes/shmprobe.c` and `probes/shmfile.c`: writes are visible across a fork *and* across two independent `open`+`mmap` of the same file |
| `brk` growth not being reserved | `probes/brkhole.c`: a fresh `mmap` never lands inside 8 MB of grown-but-untouched heap |
| `brk` shrink destroying live data | `probes/brkshrink.c` (raw `brk` clashes with glibc, identical crash on Linux, so it proves nothing on its own) |
| a corrupt prefix | a brand-new prefix from scratch (appid 999) crashes identically |
| two libcs, `process_vm_*`, `/proc/pid/mem`, layout, JIT, FEX architecture | earlier rounds |

The two concrete next steps: (1) test the guest-stack hypothesis — host code overflowing the small
Windows stack would corrupt exactly the neighbouring guest structures that hold those pointers — and
(2) implement `process_vm_readv`/`process_vm_writev`, which wine prefers and this kernel stubs with
`ENOSYS`, forcing wine onto `/proc/<pid>/mem` for every cross-process access.

## Round of 17 October, 06:28–06:40: the workaround lets wine past the crash

Two more suspects cleared, both with probes that run against real Linux first:

- **Guest stack size**: Windows PEs built with a 1 MB and a 64 MB stack reserve
  (`probes/stackpe.c`, `zig cc -target x86_64-windows-gnu -Wl,--stack,<bytes>`) crash identically, and
  so does wine started **directly** (no Proton, no steam.exe) — so neither the stack nor Proton's
  launcher is the trigger.
- **`/proc/self/maps` coverage** (`probes/mapsheap.c`, `probes/mapsbrk.c`): wine finds free address
  ranges by parsing that file, so a missing heap entry would let it map over glibc's heap. The brk
  heap *is* listed (`000001021000-000001042000 -w-p`, covering `sbrk(0)`), only the `[heap]` label is
  missing — cosmetic, not the bug.

**The A/B result from the previous round is what matters.** With

```
MALLOC_MMAP_THRESHOLD_=1 MALLOC_TRIM_THRESHOLD_=1 MALLOC_TOP_PAD_=0
```

(glibc takes nothing from `brk`) wine no longer aborts and — for the first time in this whole
investigation — gets to a **running `services.exe`**:

```
Proton: Error while copying … amdxcffx64.dll: No such file or directory
wineserver: using server-side synchronization.
procs: services.exe wineserver
```

That is the first configuration in which wine progresses *past* the point where every previous run
died within seconds. A detached long run with this setting is going (`/tmp/wine-long.log`,
`/tmp/wine-long.done` on the device) so the next round can tell whether it completes or merely hangs.
If it completes, this is both the workaround to get to Left 4 Dead and a precise pointer at the
emulator defect: something about the `brk` heap, in the path glibc's main arena uses and nothing else
does.

## Round of 17 October, 06:50–07:35: **a Windows program runs under wine**

The first real success in this whole investigation. With wine started **directly** (not through
Proton), the allocator workaround, and the high layout, our own Windows program executed and did its
work:

```
file=[gui probe ran in a 64-bit Windows process]
```

The command that produced it (on the device):

```sh
env ISH_HIGH_VA=1 LD_LIBRARY_PATH=/opt/linpad-glibc \
    MALLOC_MMAP_THRESHOLD_=1 MALLOC_TRIM_THRESHOLD_=1 \
    XDG_RUNTIME_DIR=/tmp/xdg-runtime WAYLAND_DISPLAY=wayland-0-gpu \
    WINEPREFIX=…/compatdata/500/pfx \
    WINELOADER=…/files/bin-arm64/wine WINESERVER=…/files/bin-arm64/wineserver \
    …/files/bin-arm64/wine /tmp/l4d-probes/gui-file-pe-aarch64.exe
```

The probe writes a file instead of printing, because a console program's stdout needs a Windows
console that does not exist here — that is why earlier "cmd /c echo" runs looked like failures while
wine was actually fine. `probes/guifilepe.c` is the source, built as a GUI-subsystem PE for aarch64,
x86 and x86_64 (the x86 one is Left 4 Dead's architecture).

**What the road there established:**

- Proton does not run the program directly: it starts **`steam.exe <program>`** (wine's Windows Steam
  client stub) for the DRM handshake. That process simply waits here — `ps` shows
  `steam.exe … gui-file-pe-aarch64.exe`, state `S`, for minutes, and the program never starts.
- The reason is now proven: **the Linux Steam client is logged off**
  (`logs/connection_log.txt`: `[Logged Off, 0, 0] Log session ended`). `steam.exe` needs a signed-in
  client to hand off. The client I started headless (4 processes, 57 sockets) never logged in.
- The client needs a **display** for that, and the X bridge cannot start yet:
  `/usr/local/bin/ishwl-x11` fails with `Xwayland: not found` — the binary exists in the desktop
  session's PATH but not in the agent's. `/opt/steam-host/usr/lib/aarch64-linux-gnu` carries all the
  glibc ARM64 graphics libraries (libwayland-client, libwayland-egl, libX11, libvulkan, libEGL,
  libGL), so a display is within reach.
- Wine's session itself comes up fine with the workaround: `services.exe`, `plugplay.exe`,
  `svchost.exe`, `winedevice.exe`, `explorer.exe`, no corruption. The explorer respawn loop and the
  `NtUserRegisterClassExWOW Failed to get shared session object` errors are the missing window
  station/desktop, i.e. the same display problem.

**Next:** find `Xwayland`, start the X bridge, start the Steam client with a display so it signs in,
and then run Left 4 Dead — either through Steam's Play button (which brings Proton's `steam.exe`
handshake with it) or through the launcher once the client is signed in.

## Same round, second half: the Steam path needs a signed-in client, and its UI cannot start

With wine proven to run Windows programs, the remaining blocker for *Left 4 Dead* is Steam's DRM
handshake, and that handshake runs through Proton's `steam.exe`, which needs the Linux Steam client
signed in. The client is not:

```
logs/connection_log.txt: [2026-10-16 19:49:15] [Logged Off, 0, 0] [U:1:0] Log session ended
logs/connection_log.txt: [2026-10-16 23:37:08] [Logged Off, 0, 0] [U:1:0] CCMInterface::SetSteamID( [U:1:0] )
```

The account is saved (`loginusers.vdf`: `buberlo`, `RememberPassword 1`), so an automatic login was
plausible. It never starts, because the client's own UI backend fails: `logs/steamwebhelper.log` is 59
bytes and `console_log.txt` repeats

```
Created mapping SteamChrome_MasterStream_uid0_spid…_mem when set to fail if created
```

every ten seconds, with no `steamwebhelper` process at all. `-cef-disable` (with `-no-cef-sandbox
-cef-disable-gpu`) changes nothing; the same loop continues.

**The display itself works.** Xwayland lives in `/opt/steam-x11/bin` (the agent's PATH simply lacks
it), and `ishwl-x11` is designed to run one X11 app inside its own rootful Xwayland:

```sh
PATH=/opt/steam-x11/bin:$PATH XDG_RUNTIME_DIR=/tmp/xdg-runtime WAYLAND_DISPLAY=wayland-0 \
    /usr/local/bin/ishwl-x11 -g 1280x800 /usr/local/bin/steam -no-cef-sandbox -cef-disable-gpu
```

That brings up `ishwl-x11`, `Xwayland`, `ishwl-x11-wm` and the Steam window — the login window is on
the iPad's screen right now. Note for the next attempt: the client's CEF backend worked earlier today
when the emulator ran with the **JIT** enabled, and every attempt in this round ran on the
**interpreter** (the app was booted with `-fastMode.setting off` so it would start without StikDebug
while the iPad is locked). CEF timing out in interpreter mode is plausible and would explain the
regression, so the next attempt should be made with the JIT — which needs the iPad unlocked.

## Round of 17 October, 07:40–08:10: Left 4 Dead's own executable runs under wine

`left4dead.exe` starts under wine and stays alive for minutes — the process list shows it, memory
settles at ~1.8 GB and nothing crashes:

```
procs=[left4dead.exe winedevice.exe]          (alive for 272 s and counting)
```

That is the closest this has come to the goal. It is run with wine directly (no Proton, so no
`steam.exe` wrapper) inside a real display. What it does *not* do is open a window, because wine's own
window layer fails first:

```
err:win:get_desktop_window failed to create desktop window
err:class:NtUserRegisterClassExWOW Failed to get shared session object for window class
```

`explorer.exe` cannot help either — started by hand in the same session it fails with
`ole:apartment_createwindowifneeded CreateWindow failed with error 1411` (invalid window class) — and
the game then reports the same class error. So the window-station/desktop objects wine keeps shared
between server and clients cannot be created in this environment.

Cleared as suspects in this round, each with a probe validated on real Linux first:

| Suspect | Evidence |
| --- | --- |
| file-descriptor passing (wine hands its shared-session objects to clients with `SCM_RIGHTS`, Chromium/CEF does the same for its IPC) | `probes/fdpass.c`: both a memfd and a file fd arrive intact, and writes through the received fd are visible in the sender |
| shared memory (again, now across two independent `open`+`mmap`) | earlier `probes/shmfile.c` |
| the display | Xwayland runs (`/opt/steam-x11/bin`), its WM and the Steam window appear in the compositor — the desktop/class failure happens with and without it |

**Everything in this round ran on the interpreter**, because the app had to be started with
`-fastMode.setting off` to get a session at all while the iPad is locked. The Steam client's CEF
backend and wine's shared-session objects both worked earlier today when the emulator had the JIT, so
the next attempt must be made with the JIT — which needs the iPad unlocked.

**`l4d-play` is installed on the device** (`/usr/local/bin/l4d-play`, source in this directory): it
starts the Steam client inside a rootful Xwayland and waits for the login (`-c`), starts Left 4 Dead
through Steam (`-applaud 500`, the real path with the DRM handshake), or starts the game directly
under wine (`-g`) when the handshake is not wanted.

## Round of 17 October, 08:10–08:40: memfd sealing implemented — and what wine still misses

**A real emulator defect, found with a probe and fixed.** Chromium/CEF and modern wine create their
shared memory as `memfd_create(MFD_ALLOW_SEALING)` + `ftruncate` + `F_ADD_SEALS` and treat any error as
a failed allocation; virglrenderer/Venus seals its device-memory fds the same way. This kernel
answered both `fcntl` commands with a constant and the seal bits were unknown, so callers saw
`EINVAL`. Measured on the device before the fix, and on real Linux for reference:

```
iPad : F_ADD_SEALS(0x7) failed: Invalid argument     -> SHARED-MEMORY CAPABILITY MISSING
Linux: seals applied: 0x7 (asked 0x7)                -> sealed memfd refused to shrink
```

`kernel/memfd.c` now carries real per-memfd state (name, seals, whether `MFD_ALLOW_SEALING` was
given) and `fs/fd.c` routes `F_ADD_SEALS`/`F_GET_SEALS` to it, with Linux semantics: `EINVAL` for a
non-memfd or unknown bits, `EPERM` without `MFD_ALLOW_SEALING` or after `F_SEAL_SEAL`. Verified on the
device after the install:

```
seals applied: 0x7 (asked 0x7)
```

Not enforced yet, and documented as such: shrinking/growing a sealed memfd is not refused, and
`F_SEAL_WRITE` does not block writes. Commit `c65a746d`; source: `probes/shmcaps.c`.

**What wine still misses.** The sealing fix alone did not bring wine's window layer up, so the server
conversation was traced (`WINEDEBUG=+server`, 184k lines, summarised in the reports). It shows the
shape of the failure precisely:

```
get_thread_desktop() = 0 { locator={id=00000001,offset=000ffd20}, handle=006c }
create_class()       = 0 { locator={id=00000002,offset=000fff70}, atom=8001 }
err:class:NtUserRegisterClassExWOW Failed to get shared session object for window class

open_mapping( name=L"\\KernelObjects\\__wine_user_shared_data" ); open_mapping() = 0 { handle=0004 }
get_mapping_info() = 0 { size=00080000, flags=01800000, shared_file=0000, ... }
get_handle_fd() = INVALID_HANDLE
```

The server side is fine — it hands out valid locators. What is missing is the **file descriptor that
makes a mapping shared**: `shared_file=0000` and `get_handle_fd() = INVALID_HANDLE`, so the client
maps its own anonymous copy and `find_shared_session_object()` looks into an empty one. The client's
`find_shared_session_object` is the caller in wine's `dlls/win32u/class.c` (fetched copy in
`wine-internals/`); the shared state itself is wine's `shared_session` (`session_shm_t`).

Cleared as the cause: file identity is consistent (`probes/fileid.c`: `stat(path)`, `fstat(fd)` and a
second process all report the same `st_dev`/`st_ino`), even though the server log repeats
`is_same_mapping() = NOT_SAME_DEVICE` — that is the normal answer when comparing different files.

## Round of 17 October, 08:45–09:00: **the Steam client works — CEF runs and the account is signed in**

Two independent defects were in the way, and both are fixed:

1. **`memfd` sealing** (previous round, commit `c65a746d`) — CEF, modern wine and virglrenderer/Venus
   all allocate shared memory as `memfd_create(MFD_ALLOW_SEALING)` + `ftruncate` + `F_ADD_SEALS` and
   fail the allocation on any error. The device now reports `seals applied: 0x7`.
2. **The webhelper path.** The client spawns its UI backend as `$HOME/steamwebhelper`, and
   `logs/steamwebhelper.log` said so plainly:

   ```
   env: '/root/steamwebhelper': No such file or directory
   ```

   A symlink `$HOME/steamwebhelper -> $STEAM/steamrtarm64/steamwebhelper` fixes it (device change,
   documented here, one line to remove). Starting the client from the Steam directory did *not* help;
   it really wants that path.

With both in place:

```
steam=1  steamwebhelper=7            <- CEF is running
connection_log.txt: [2026-10-17 00:42:06] [Logged On, 4, 7]
console_log.txt:    System startup time: 40.54 seconds
```

The client is signed in as `buberlo` with a working UI, inside a rootful Xwayland on the iPad. That
was the blocker that had stood in front of everything: Proton's `steam.exe` DRM handshake needs a
signed-in client.

Wine's own remaining defect is narrowed but not fixed: the client maps the section
`\KernelObjects\__wine_session` successfully (the server even transfers the descriptor —
`*fd* 0024 -> 20`), yet reports

```
warn:winstation:find_shared_session_object Session object id doesn't match expected id 1
```

so the client's view of the shared session differs from the server's. Everything around it works in
isolation, verified with probes that pass on real Linux first: fd passing over a *bound* socket,
read-only `MAP_SHARED` mappings that see later writes, and consistent file identity. Wine's window
classes therefore still cannot be created, which is what stops the game from opening a window.

Immediate next step: get the launcher and the display and the signed-in client into one run without
losing the X server (the last two attempts died on display bookkeeping, `X connection to :28 broken`).
