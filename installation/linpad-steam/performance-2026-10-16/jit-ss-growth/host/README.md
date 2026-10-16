# Single-step PC lookup growth

Build 4448 removed the previous page-invalidation scan. Its physical iPad
startup profile then attributes 4.612 of 5.518 weighted sample seconds (83.58%)
to `ss_get`, at the chained PC lookup. The existing cache always has 256
buckets even when many fallback instructions are live.

A later SwapChain-stage trace has 9.435 weighted sample seconds over a 5.436 s
span across threads. The saved path summary attributes 3.455 weighted seconds
to mutex wait through `ss_get`, 2.046 to its unlock/wake path, and 3.142 to
`ss_get` itself. These are statistical sample weights, not elapsed gameplay
or frame-rate measurements.

## Change and safety boundary

The PC hash grows geometrically at a live-entry load factor of one. Rehashing
and lookup use the existing per-mm lock. Every incoming PC-chain backlink is
repaired before the old bucket array is freed. Page-index links, block/entry
addresses, generation checks, and deferred reclamation are unchanged. The
bucket calculation now occurs after locking because the size can change.
Allocation failure retains the previous valid table; another attempt waits
until the live count doubles, then capacity catches up. Allocation-size
arithmetic is checked for overflow.

The existing shared hash function is retained. The synthetic 65,536-entry
comparison covers high addresses with 4-, 64-, 4096-, and 65536-byte strides.
It measures exact successful-lookup node visits and local elapsed time. The
4-byte pattern still clusters somewhat, so growth does not promise a strict
constant-time bound for all possible addresses. No last-hit cache or new
lock-free lifetime protocol was added.

## Verification

`test-jit-ss-index.py` extracts the actual production functions into a host
fixture; translation, compiled-block invalidation, and allocator failure are
controlled stubs. ASan/UBSan checks cover lookup identity through eight resizes,
page removal and reinsertion after rehash, generation semantics, one-shot
entries, failure backoff/recovery, overflow, randomized range survivors, and
100,000 concurrent cache hits during growth. The original invalidation
benchmark keeps its historical 256-bucket PC layout; the separate lookup
benchmark compares a fixed 256-bucket control with the production growing
lookup. See the exact run logs and provenance rather than infer device speed
from these host timings.

The source was independently reviewed. App/CLI rebuild, unchanged real ARM64
SMC/Mono controls, and physical iPad startup/profile comparison remain later
gates owned by the parent task. This folder does not claim gameplay or FPS.
