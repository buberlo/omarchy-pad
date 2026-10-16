# Build 4450 startup CPU audit

This is read-only analysis of the physical iPad trace already recorded by the
parent task. Build 4450 uses source `f93f1449` and executable UUID
`5FB3CB79-8481-3307-97C7-5EEFD91C69D2`. Host PID is 21521; game guest PID is 271.
The Instruments interval is **13:14:45.059–13:14:50.844 UTC** on 2026-10-16.
The 6,226 samples contain 6.226 weighted CPU seconds across a 5.444-second sample
span. These are statistical CPU samples, not elapsed startup time or game FPS.

## Findings

| Sample category | Weighted CPU seconds | Share |
| --- | ---: | ---: |
| Numeric/generated/unsymbolicated self PCs | 2.702 | 43.40% |
| `_platform_memmove` directly under `gadget_step` | 0.995 | 15.98% |
| Named `gadget_*` self PCs | 0.450 | 7.23% |
| `ss_get`, inclusive | 0.429 | 6.89% |
| Mutex kernel wait/wake directly under `ss_get` | 0.201 | 3.23% |
| `ss_get`, self only | 0.110 | 1.77% |

Categories overlap. The main game thread has 5.247 weighted seconds (84.28% of
the trace). The worker threads that dominated the previous build's later
contention trace contribute very little here. This startup interval therefore
does **not** establish whether the context cache eliminated that later
multithreaded bottleneck. A comparable later phase is needed for that gate.

The `memmove` samples resolve to the two call sites surrounding `fiber_enter`
inside `gadget_step`: copying `ctx->cpu` into the fiber frame and copying it back.
They are not evidence of frame-buffer or asset-copy overhead. Named fiber entry
and exit also appear. The remaining `lookup_lockfree` self cost is 0.145 seconds
(2.33%); it is smaller than fallback-state copying in this interval.

The report collected during the trace contributes a measurable observer effect:
0.077 seconds (1.24%) is `mincore -> mm_footprint -> ... -> oom_show_memory` from
reading `/proc/ish/memory`. Report helper processes also appear. Prefer avoiding
that read inside another short profile. No device setting was changed here.

## Interpretation and next measurement

The status report at 13:14:50 shows Vulkan selecting Venus/Apple M2 at 13:14:05,
Mono assemblies loaded at 13:14:11, swap-chain setup at 13:14:24, and Steam API
initialization failure at 13:14:47 because this direct launch lacks a running
Steam client. The trace spans that Steam initialization interval. It does not
prove a later stall, a visible frame, or gameplay. The report records zero frame
log lines at that instant, 1,655 MiB app footprint and zero OOM kills.

The measured next research target is **fallback transition frequency**, if this
cost remains large in the later profile. It is not yet a justified instruction
implementation or CPU-layout change. Existing counters cannot identify every
hot fallback: `stats_fallback` counts generated sequences, while the opcode
histogram counts `JR_FALLBACK` exits and omits direct negative-block dispatch.
A bounded observation covering both entry paths would be needed before choosing
which native translator gap to implement.

## Source boundary

In `jit/jit.c`, a cached negative `jit_block` with `rx == NULL` deliberately calls
`gadget_step`; there is no negative-entry promotion counter. `ss_get` performs a
generator step and retains a `fiber_block`. Its code array contains gadget
addresses and operands, as emitted by `asbestos/guest-arm64/gen.c`, not native
JIT block instructions. Its CPU-state, FPCR and fault-recovery conventions differ
from the native block ABI. Persistence alone cannot turn it into `b->rx`.

The usual negative entry is an unsupported first native instruction. An
unreadable instruction fetch or oversized translation can also produce one.
Host/code memory pressure instead uses the separate retry result and transient
fallback without installing a permanent negative entry. MSR FPCR is one
confirmed translator gap used in our existing fallback SMC regression, but this
profile does not establish that it dominates the game.

`startup-summary.json` and `startup-call-paths.json` contain exact aggregates;
`analyze-paths.py` reproduces the latter. `startup-toc.xml` contains the trace
timing metadata. `provenance.json` records input/source hashes. No app sources,
guest configuration, device process or frozen release archive were changed.

## Later loading profile

The separately recorded `loading-profile-samples.xml` contains 15,343 samples,
15.343 weighted CPU seconds and a 5.557-second sample span. The user-visible
state reported by the parent was a black loading window with a loading icon.
This is not the later title-menu cadence measurement.

The shared SS lookup remains significant: `ss_get` inclusive is 6.180 seconds
(40.28%), including 5.103 seconds (33.26%) in its mutex wait/wake paths. Its own
lookup body is 0.380 seconds (2.48%). CPU-state copies under `gadget_step` consume
2.034 seconds (13.26%). Loading.Pre, the main game thread and UnityGfxDev account
for 28.44%, 27.74% and 25.29% of the weighted samples respectively. Numeric
unsymbolicated/generated self PCs account for 26.00%.

This confirms residual shared-lock pressure despite the new context cache. It
does not identify whether stale address-space generations, direct-map cache
collisions, or a large fallback working set cause the misses. The next justified
diagnostic is an opt-in bounded per-context miss-reason count plus recent
randomly sampled PC/opcode records from both fallback entry paths. Increasing
the cache or implementing an assumed hot opcode is not yet justified. That
diagnostic is separate pending work, not part of build4450.

No `/proc/ish/memory` mincore observer path appears in this later profile. The
parent deliberately avoided status probes during measurement; a few shell
threads occur at very small weights, so this should not be described as an
exclusive game-process profile. Exact aggregates and paths are in
`loading-summary.json` and `loading-call-paths.json`; timing metadata is in
`loading-toc.xml`. No same-scene throughput ratio is inferred across builds.
