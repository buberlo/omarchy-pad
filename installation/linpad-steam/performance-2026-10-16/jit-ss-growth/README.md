# Growing fallback JIT lookup table

Commit `df8a7720` grows the fallback PC lookup table geometrically instead of
retaining 256 buckets for arbitrarily many live instructions. It follows the
physical build4448 profile that identified `ss_get` and its mutex paths as the
next startup bottleneck. All table readers and growth use the existing lock;
rehash repairs predecessor links and preserves page indexes, node addresses,
generation checks and deferred reclamation. Allocation failure retains the
old table with bounded retry frequency. The source and independent review
explain overflow handling and remaining hash-clustering limits.

The host actual-function harness passes 338,461 assertions with ASan/UBSan,
including concurrent hits during eight resizes, identity preservation,
invalidation/reinsertion, OOM recovery and overflow checks. Both the historical
invalidation benchmark and four-stride lookup benchmark pass. Host timings and
node counts are synthetic workload evidence, not game FPS predictions.

The rebuilt local CLI also contains the scheduler-query fix later committed
as `d8fa5c1e`. Its SHA256 is
`2f8a87fc674799138ee99226292528a8bb498fb22e768e0ab111f17e0073d007`.
Both runtime provenance records name that same CLI; the recorded JIT and
scheduler source hashes are verified against committed blobs in `d8fa5c1e`.
The records retain the then-current HEAD `df8a7720`, because the scheduler
change was still uncommitted when those controls were captured.

All four real ARM64 code-modification controls pass: ordinary and actual
MSR-FPCR fallback SMC, each in normal JIT and tier 2. Exact Mono initialization
also returns a nonnull domain and exit 0 with no SDL and full embedded SDL using
the local dummy display. These are local functional checks. No physical
build4449 result, rendered game frame, sound, playability or FPS is inferred.

`source/` contains exact `df8a7720` blobs and `source-change.patch` is the
committed change. `host/` preserves the earlier analysis/provenance verbatim;
its precommit status statements describe that earlier capture, while this
README and root provenance record the completed combined CLI controls.
`runtime-controls/` contains small own-process logs and Mono excerpts. Large
raw syscall logs, CLI binaries and proprietary Unity/Mono/game bytes are
excluded; omitted raw logs retain size/hash records. New app/device evidence
belongs in the separate build4449 archive.
