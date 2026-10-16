# FMOD thread initialization: Linux scheduler query repair

LinPad's priority-bound queries rejected every policy except SCHED_OTHER. The exact x86 glibc used by Hollow Knight calls these queries when validating `pthread_attr_setschedparam`. As a result, the game's embedded FMOD Linux thread helper rejected priorities90/94/99 and returned33 before creating a worker. This isolated failure is reproduced on physical build4448 and fixed in matched local and physical build4449 tests. The full-game audio and presentation gates remain pending at this archive checkpoint.

Source commit `d8fa5c1e` (regression source committed separately as `060b3823`) returns Linux ABI bounds: FIFO/RR min1/max99; OTHER/BATCH/IDLE/DEADLINE/EXT min0/max0; unknown policies EINVAL. ARM64 syscall126 now calls a distinct minimum function; the missing i386 syscall160 entry is added. The scheduler setters are unchanged. Querying a permitted attribute range does not grant realtime scheduling. This follows the primary [Linux v6.12 scheduler syscall implementation](https://raw.githubusercontent.com/torvalds/linux/v6.12/kernel/sched/syscalls.c).

## Exact failure path

The locally installed UnityPlayer has SHA256 `12ce7af93c41b13b3e7d9b75b591336cc56c9ce113714143fd897cc1dcf00e33`. Its original Linux thread helper at ELF address0x84ecb0 uses tables at0x3312a0/0x3312c0: positive priority enums1/2/3 select SCHED_FIFO with priority90/94/99. The call to `pthread_attr_setschedparam` is at0x84ed61; a nonzero result branches to the common return33. The helper does not call `pthread_attr_setinheritsched`: default inherited scheduling remains active.

The exact x86 glibc function at0x90d80 calls both priority-bound queries and returns EINVAL if either is negative. This is why rejecting the policy breaks attribute validation even when no realtime scheduling is requested. `exact-helper-disassembly.txt` and `glibc-attr-disassembly.txt` are narrow excerpts with input hashes in provenance. No proprietary library, full disassembly, game assets, credentials or broad process maps are archived.

The isolated Unity probe loads the exact local library, resolves its base through the exported PlayerMain symbol without calling it, verifies the helper's first16bytes, and calls only the original thread helper. Each worker records its actual Linux scheduler policy and completes. The device wrapper additionally checks the full Unity SHA256 before loading. It never initializes FMOD's System, creates audio/graphics streams, enters the game, changes code, or modifies the running game.

## Matched validation

All final comparisons use identical diagnostic binaries. Local baseline CLI SHA256 is `ef0e4c1a0e8e114c9c0ead0db1e89fe3fccee45c271b07b3439731b7bd2949bd`; combined corrected CLI is `2f8a87fc674799138ee99226292528a8bb498fb22e768e0ab111f17e0073d007`. The latter also contains the independently validated JIT table-growth change; the scheduler query/attribute change is directly observable in the scoped outputs.

| Probe | Before | After, local CLI and physical4449 |
| --- | --- | --- |
| Native ARM64 scheduler queries and inherited workers | 12 failures /45checks | 45/45PASS, exit0 |
| x86 glibc via patched FEX | 15 failures /39executed checks; priorities90/94/99 return EINVAL | 45/45PASS, exit0 |
| Exact embedded FMOD thread helper | priority0 works; priorities1/2/3 return33 with no worker | all4 return0, all workers complete with SCHED_OTHER, exit0 |

The x86 baseline executes fewer checks because failed attributes correctly prevent its worker creation stage. Native musl accepts those attributes without querying the kernel, so it reaches the inherited-worker checks even before the fix. The regression deliberately uses the direct Linux `sched_getscheduler` syscall for worker policy because musl's public wrapper is an ENOSYS stub.

Physical build4448 independently reproduced the final scheduler failures at12:41:58UTC and exact embedded helper returns33 at12:43:09UTC. The complete scoped reports are included. Physical build4449 then passed the identical final native and x86 scheduler probes45/45 each, and all4 exact Unity helper cases returned0 with worker_done1 and policy0. The matched physical after reports are `sched4449-device-pass.txt` and `fmod-thread4449-device-pass.txt`. No explicit audio-routing override was enabled for the subsequent game launch; its outcome is outside this frozen diagnostic checkpoint. The earlier v1 scheduler probe used musl's stub and incorrectly added three native worker-policy failures; it is retained as explicitly superseded evidence and is not used in the matched result table. Its x86 attribute failure was valid. i386 dispatch was reviewed but not independently executed in this ARM64 qualification.

The separate `audio-path-audit` archive proves that x86 Pulse can connect to the native ishaudio socket and enumerate its sink. That connection test and this exact thread-helper test do not prove full FMOD initialization, PCM output, audible sound, a visible game frame, or playability. Build4448's actual game logged FMOD33 including the nosound fallback, then continued assembly loading. The query fix is a proven blocker repair; further full-game validation is required.

## Reproduction

`build-probes.sh` builds the authored diagnostic sources using the existing workspace Zig compiler. Run it from the workspace root, optionally setting `PROBE_DIR` to this archive directory. `launch.c` is the small native ARM64 launcher used by local FEX tests. Stage these diagnostics under the isolated root's `/opt/fmod-probe`; the exact Unity library remains a local input at `/opt/UnityPlayer.so` and must not be copied into this archive. `run-local.py OLD_ISH NEW_ISH ROOTFS OUTDIR` runs bounded native/FEX/helper probes and captures separate raw logs plus narrow excerpts. Archived results were generated with the equivalent commands before this reusable runner was written.

The final served `sched4448-probe` and `fmod-thread4448-probe` are operator-run device entrypoints. Each downloads only its diagnostic payload, verifies hashes, runs with a bounded timeout, and posts a small report even when the baseline fails. Their historical names are retained so the same exact scripts can be used after4449. No production audio configuration or environment hook was installed.
