# Exact Mono initialization diagnostic

Local macOS-hosted LinPad CLI tests, using the same patched FEX and exact Mono/core assembly as Hollow Knight. No app installation or physical-device actions were performed by this diagnostic. Proprietary game binaries remain local and must not be copied to repository or release assets.

Inputs and source hashes are in `inputs-and-diagnostic-source.json`. The exact Mono library is SHA256 `682d74f03efd9cebd05d38e82c4b296a0a1865f070440281aa1ac51022220b94`; matching `mscorlib.dll` is SHA256 `19c81960f79980f14cd4dc271ed42c3e7f031f024c17977fc0f4a9f672a23b86`. Their paths and sizes are recorded without embedding their bytes. The Unity embedded SDL entrypoint has a machine-code guard and is the same entrypoint used in the earlier subsystem diagnostic.

## Controls before the host mapping fix

`GC_init` followed by `GC_gcollect` passed with no SDL, SDL timer initialized before GC, SDL timer initialized after GC, and SDL flags `0x3221` initialized before GC. All four exited0. The full SDL control explicitly uses the dummy video backend because the local CLI has no iPad compositor. It does not qualify the game's Wayland path.

With the matching core assembly present, `mono_jit_init_version("LinPadMonoProbe", "v4.0.30319")` failed to return in all four corresponding cases. Each was ended by the local test runner at its explicit45-second deadline, not by a natural crash exit. The no-SDL case alone reproduces the FEX-side protected-memory fault and subsequent futex wait. The case that initializes SDL after Mono never reaches its SDL call.

`mono-results.json` and `gc-results.json` hold exact results. The public initialization API follows the [Mono embedding guide](https://www.mono-project.com/docs/advanced/embedding/). The probe calls neither game managed assemblies nor PlayerMain. It exits after successful initialization/collection to avoid unrelated Unity global destructors.

## Confirmed fault chain

The optional x86 mprotect observer forwards the original arguments and return/errno unchanged. It logs the caller and pthread stack bounds outside a signal handler; it adds diagnostic work and is not a timing benchmark.

1. `pthread_getattr_np` reports main-stack base `0x800137d45000`, size `0x7ed02bb000`, while the actual stack local is near `0x808007fffb1c`. A direct x86 `getrlimit(RLIMIT_STACK)` control reports soft `0x8000000` (128MiB), hard `0xffffffffffffffff`, success. The giant stack range is therefore not the requested soft limit.
2. Mono calls `mprotect(0x800157d5f000, 0x8000, PROT_NONE)` from library offset `0xbd91f`. Static disassembly shows this is the stack-overflow guard path: returned stack base plus one page, with an aligned32KiB guard.
3. This range contains live FEX map nodes. The local ARM64 FEX accesses at PC `0xef94a2a4`/`0xef94a2b0` fault on `0x800157d640b0`. They are read/write operations in `std::_Rb_tree_insert_and_rebalance`, called from the map insertion return address `0xefb2c100`.
4. The exact file's first mapped segment starts at `0xef782000`, so the corresponding ELF virtual addresses are `0x1c82a4`, `0x1c82b0`, and `0x3aa100`. LinPad's console-relative offset calculation is unsuitable here: subtracting a later segment's file offset misidentifies ELF virtual addresses because PT_LOAD virtual and file offsets differ.

See `mono-0-mprotect-excerpt.txt`, `stack-limits-before-excerpt.txt`, and the complete local logs. The independent FEX/host-source audit identifies a falsely extended final `/proc/self/maps` interval, which makes glibc's stack-size adjustment underflow and select a lower boundary inside FEX allocation space. Host-source correction and regression qualification are tracked by the graphics agent.

## Multi-block diagnostic

The only option added in `nomb-launch` is `FEX_MULTIBLOCK=0`. The same no-SDL Mono probe then returned a nonnull domain and exited0 in12.528seconds. However, full SDL`0x3221` plus Mono still hit the same FEX map-node fault and timed out45seconds. This is an allocation-layout dependent avoidance, not a semantic fix or a game-performance result.

The normal launcher does not set this option. Its default probe, launcher and patched FEX bytes were rechecked unchanged before the planned host-fix comparison. Logs show all searched FEX Config.json and probe-specific AppConfig files absent, so the baseline uses the pinned FEX default multi-block setting.

## Reproduction

Run `sh work/performance/mono-init-probe/build.sh` from the workspace root. Put the owned matching core assembly at `work/performance/vulkan-repro-root/opt/mono-init/data/Managed/mscorlib.dll`, with a local symlink from `data/mono/4.5/mscorlib.dll` to `../../Managed/mscorlib.dll`. The scratch runtime also needs its already prepared FEX, x86 runtime, and Unity library.

```sh
python3 work/performance/mono-init-probe/run-local.py gc
python3 work/performance/mono-init-probe/run-local.py mono
# Preserve baseline logs when validating the host fix:
python3 work/performance/mono-init-probe/run-local.py mono after-maps main-controls
```

Each case has a45-second host deadline and records whether the runner terminated it. The runner never signals the iPad. Normal probe runs have no diagnostic preload. The observer and fault-reporter launchers are separate binaries and their results are separately named.

## After the host mapping fix

The unchanged normal x86 probe, ARM64 launcher and patched FEX bytes were run again after rebuilding only the local LinPad CLI with the `/proc/maps` contiguous-region correction. No SDL: Mono returned a nonnull domain and exited0 in13.794seconds. Exact embedded SDL0x3221 followed by Mono also returned a nonnull domain and exited0 in27.971seconds. Neither hit the45-second deadline. The latter still uses the local dummy video backend. These host elapsed times include initialization and tracing; they are not physical-device speed or gameplay results.

The optional after-fix observer independently confirms main-stack base `0x808000000000`, size `0x8000000`, equal to the128MiB soft limit. Mono now protects `0x808000001000` for32KiB, inside that actual stack, from the same library return offset `0xbd91f`; it returns a nonnull domain and exits0. Its later worker-thread guard also lies within its reported2MiB thread stack. The stale errno value22 printed after a successful mprotect is not a call failure.

The standalone read-only x86 `stack-limits-probe.c` adds a direct glibc and maps regression. Against the preserved old CLI it exits1: the inferred stack exceeds the soft limit and the mapping extends through `0x1000000000000`. With the corrected CLI the same binary exits0: main-stack size128MiB and its mapped terminal interval ends at `0x808008000000`. This is separate from the native host sparse-map test.

Evidence: `after-maps-mono-results.json`, `after-maps-mono-provenance.json`, `stack-limits-after-excerpt.txt`, `mono-0-mprotect-after-maps-excerpt.txt`, and `stack-limits-probe-results.json` with matched before/after excerpts. The correction is in the host `/proc/maps` serializer; no Mono, Unity, glibc or FEX bytes changed.

## Prepared physical-device check (not yet executed)

`mono4447-probe` verifies hashes, creates narrow diagnostic symlinks to the already installed owned game's Unity/Mono/core-assembly files only when missing, and downloads the unchanged Mono probe plus the standalone stack check. Existing diagnostic destinations must have the expected bytes. It then removes the multi-block environment override and diagnostic preload, selects the real Wayland backend, and runs the stack check plus no-SDL and full-SDL Mono startup with bounded deadlines. It does not change Steam launch settings or execute the game. Physical results and full gameplay remain separate pending gates.
