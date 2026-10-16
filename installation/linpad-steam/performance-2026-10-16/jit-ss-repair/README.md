# JIT single-step cache invalidation repair

Physical build 4447's bounded Time Profiler capture found 95.60% of sampled
CPU weight in a whole-cache invalidation scan. The fix indexes fallback
instructions by guest page and preserves exact page matching, locked updates
and deferred reclamation. Source commit: `d4ea1429`; target app build: 4448.

The host harness runs actual extracted production functions and passes
240,144 assertions with ASan/UBSan. Its 1,000 false-positive page writes visit
1,000 nodes with the new index versus 65,536,000 with the old scan. Synthetic
microbenchmark timing is not a game-startup or FPS prediction.

Real ARM64 execution checks pass 200 code rewrites in normal JIT and tier 2.
The dedicated `MSR FPCR` fallback probe also passes both modes; JIT counters
confirm 201 fallbacks, 200 invalidations and 200 retired single-step entries.
That test source is a separate commit, `e334e1b1`, after the app's production
source pin. It was not compiled into the app.

`source/` contains exact committed source blobs, with their commit/hash records
in `provenance.json`. `production-fix.patch` is the committed change.
`host/` contains the test source generated from the production functions,
sanitizer/benchmark results and replayable native-profile summarizer.
`guest-controls/` contains the narrow local CLI checks and independent review.
Guest binary hashes are recorded; binaries and raw Instruments traces remain
local. No proprietary game libraries, game data, device IDs or raw process
inventories are included.

For the host test, run the canonical script from the LinPad checkout:

```
python3 tests/manual/steam-compat/test-jit-ss-index.py --out /tmp/linpad-ss-index
```

See `host/analysis.md` for invariants and limitations. New physical build4448
gates are maintained separately in `../build4448`. These local checks do not
establish a rendered game frame, playability or steady-state performance.
