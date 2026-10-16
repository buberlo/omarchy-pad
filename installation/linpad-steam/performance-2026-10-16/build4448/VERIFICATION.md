# Build 4448 verification

LinPad 1.4.0 build 4448 replaces the fallback JIT cache's full invalidation scan with an index by guest page. In the previous build4447 five-second CPU profile, that scan accounted for 95.60% of sampled CPU weight during Hollow Knight startup.

Built and signed from `d4ea14293dc2526e9a14de55f676adcdf88e4e31`; signed IPA and matching dSYM passed CRC/content verification, matching UUID `A5CFFE3B-C626-3F4A-A0CB-50CEC07154EB`. Both approved memory entitlements remain enabled. Installed and booted on the physical iPad with 512 MiB native JIT; physical ordinary and fallback self-modification checks both pass 200 rewrites.

The local actual-function harness passes 240,144 ASan/UBSan assertions. Real local ARM64 ordinary and fallback self-modification probes pass in normal JIT and tier 2. The guest fallback test source was committed separately as `e334e1b1` after the app's source pin and is not compiled into the app. See `../jit-ss-repair` for exact source and tests.

New-build game rendering, playability and steady-state performance remain pending. Prior Mono/GPU results are historical and are not counted as build4448 validation. Installed FEX/Mesa/EGL/Steam/game data are separate from the IPA. This is a development checkpoint.

The immutable initial artifact record is copied as `initial-build-manifest.json`; the original `record-artifact.py` was not modified or rerun. `curate-build4448.py` reads completed uniquely named reports and updates this separate curated record. It performs no device, network, staging or commit actions.

The executable and dSYM DWARF hashes were rechecked during curation. The original recorder checked every ZIP member against its source bundle and all CRCs; its log and exact archive hashes are preserved. The curation does not claim a second full ZIP traversal.

`SHA256SUMS` covers the large IPA and dSYM release assets, retained outside Git. `REPOSITORY-SHA256SUMS` covers this small curated directory. `source-evidence.json` fingerprints the original logs and identifies narrowed excerpts. Raw device IDs, process dumps and console logs remain local. Source snapshots match committed blobs.

The measured prior-build bottleneck, exact fix and local controls are in the adjacent `../jit-ss-repair` directory. Physical probes are separate gates; a completed probe does not establish gameplay or frame-rate quality. Release upload/digest verification has not yet been recorded here.
