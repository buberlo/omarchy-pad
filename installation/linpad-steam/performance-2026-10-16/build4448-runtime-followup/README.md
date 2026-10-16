# Build4448 runtime follow-up snapshot

This is separate from the frozen build4448 release/artifact proof. The actual
game launched at 12:28:50 UTC and logged Vulkan API initialization at
12:29:03.958 (about 14 seconds), loaded Mono assemblies at 12:29:35.456 and
selected PhysX at 12:29:36.794. The previous build4447 run logged the same
Vulkan API milestone at 12:05:49.625 after its 11:57:56 start (about 474 seconds).
These are individual observed startup stages, not a matched repeated benchmark,
time-to-first-frame measurement or steady-state speedup claim. Cache/thermal
state was not experimentally controlled.

The new bounded own-app CPU capture has 5,518 samples / 5.518 seconds of sample
weight. `ss_get` now accounts for 4.612 seconds (83.58%) of self weight. There
are no self samples in `ss_invalidate_locked` in this capture. The profiles
were taken at different startup stages; the result identifies the next cache
lookup bottleneck and does not by itself prove overall game performance.

No rendered game frame or playability was confirmed at this snapshot. The
prepared frame collector and host parser are in `diagnostics/`; exact usage
and interpretation are in `FRAME-CAPTURE.md`. These diagnostic tools were
checked locally, not executed on the device by the archiving agent.

Raw traces, device identifiers, process inventories, proprietary game binaries
and complete game logs are excluded. Numbered milestone excerpts retain source
hashes in `provenance.json`; the reproducible profiler summarizer is included.
Build4448 source remains `d4ea1429`. Any subsequent cache-lookup repair must
have its own source/build evidence and must not be retroactively attributed
to this already-signed app.
