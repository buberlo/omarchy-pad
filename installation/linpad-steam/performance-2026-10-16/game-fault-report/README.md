# Diagnostic x86 game fault reporter

This is a per-launch diagnostic preload. It is not a production dependency or a performance configuration. It reports the first 32 guest signals (SIGSEGV, SIGBUS, SIGILL, SIGABRT), guest RIP/registers, relevant `/proc/self/maps` entries, and the RIP's file offset. It observes handlers installed later through public `sigaction`/`signal`, then chains them. Direct signal syscalls or hidden libc entry points can bypass interposition. A reported signal can be intentionally handled by the application; correlate it with subsequent logs rather than calling every record fatal.

Local Mac LinPad CLI → old FEX tests passed: a handler installed after the constructor recovered from a null fault, action queries preserved the original handler, and `signal` handlers were chained (exit 0). Resetting SIGSEGV to its default still terminated with exit 139 after reporting the fault. `local-results.json` records raw log hashes. A physical game execution has not yet qualified this reporter.

Served files:

- `work/performance/serve/x86-game-crash-report.so`, SHA-256 `90ddd9710b54670480eefcea2dc19317f75f4185a4ea35bd3018d6a4be82266c`.
- `work/performance/serve/fex-game-crash-report`, SHA-256 `52177b933138d861c6b945d5af42be67f3638f44bc9bd90a67c66393486fac24`.

Place both in `/tmp` and make the wrapper executable. For one existing native-game launcher invocation, set `LINPAD_FEX_BINARY=/tmp/fex-game-crash-report` and `LINPAD_FEX_REAL_BINARY` to the patched FEX executable. The wrapper supplies the preload after the native Steam tool sanitizes its environment. The shared object defaults to `/tmp/x86-game-crash-report.so`; `LINPAD_X86_CRASH_REPORTER` can select another path. The wrapper requires an existing executable and readable shared object; it does not download or install anything.

The expected startup marker is `X86_FAULT_REPORTER_READY`. Save `X86_FAULT`, `X86_MAP`, and `X86_RIP_FILE_OFFSET` records from the game's output. Reverting the launch selection removes the observer; no system configuration is changed.

## Build 4445 old-FEX game failure

Source logs: `work/performance/reports/hollow4445-first-status.txt`, `work/performance/reports/hollow4445-munmap-diag-exit.txt`, and `work/performance/build4445/device-console.log`.

The game advanced past Mono and input initialization, but SDL reported a null window backend, an uninitialized video subsystem and a 0×0 desktop. It eventually returned 139 at 10:03:16 UTC. The host app remained running; the X connection shutdown follows guest exit.

The relevant terminal fault sequence in the console is:

- Line 338: process 320, signal 11, code 1, address 0, generated ARM PC `0x800829f52a1c`, LR `0x800124041158`.
- Lines 340–343: code 0 with address `0x140`, preceded by Linux syscall 129 (`kill`) targeting 320; then `sigaction`, `getpid`/`gettid`, syscall 131 (`tgkill`), and `rt_sigreturn`.

`0x140` equals process ID 320. LinPad's kill path stores the sender PID in the signal union, while its diagnostic prints that union as a fault address for all signal types. Therefore the final `0x140` is self-signal metadata, not evidence of a load from address 0x140. FEX's unhandled-signal path resets the disposition and re-sends terminating signals. Earlier code-2 faults around `0x7ffde000` recovered repeatedly, with later progress and page-protection changes; they are not the terminal event. FEX has a dedicated main-thread stack growth handler for access-permission faults.

The null fault's original x86 RIP/library cannot be recovered from these logs alone. Its generated PC differs from the old fixed dispatcher null-probe PC. The SDL failure is a strong preceding symptom, but the logs do not establish which Unity/SDL instruction later dereferences null. The reporter is intended to identify that instruction on a subsequent launch.

An alternative diagnostic is `FEX_BLOCKJITNAMING=1`, present in the current binary. FEX writes `/tmp/perf-<pid>.map` entries mapping generated blocks to guest RIPs or library offsets (`FEXCore/Source/Common/JitSymbols.cpp`, `Interface/Core/Core.cpp`). Entries are buffered and may be incomplete at a fatal exit; this option adds diagnostic overhead and must not be used for performance claims.

This repository copy contains the diagnostic source, exact wrapper, probe/launcher sources, result metadata and curated trace excerpts. Full local traces remain at `work/performance/game-fault-report/local-chain.txt` and `local-fatal.txt`; hashes in `local-results.json` identify them. The shared object is served separately and is not installed by this archive.
