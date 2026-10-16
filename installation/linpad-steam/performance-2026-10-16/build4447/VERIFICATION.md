# Build 4447 verification

LinPad 1.4.0 build 4447 fixes incorrect `/proc/self/maps` boundaries that caused glibc to infer a false main-stack range and Mono to protect live FEX metadata.

Built and signed from `9beb5dea2eeaffd7ebd27952592cba658fe12bb4`; signed IPA and matching dSYM passed CRC/content verification, matching UUID `DEDA2762-5AAF-3685-9513-27727BEF64C6`. Both approved memory entitlements remain enabled. Installed and booted on the physical iPad with 512 MiB native JIT; physical native map-boundary regression passes 10/10; physical default-FEX Mono initialization passes with no SDL and with the real Wayland SDL backend.

Game rendering, playability and steady-state performance remain pending. Installed FEX/Mesa/EGL/Steam/game data are separate from the IPA. This is a development checkpoint.

The immutable initial artifact record is copied as `initial-build-manifest.json`; the original `record-artifact.py` was not modified or rerun. `curate-build4447.py` reads completed uniquely named reports and updates this separate curated record. It performs no device, network, staging or commit actions.

The executable and dSYM DWARF hashes were rechecked during curation. The original recorder checked every ZIP member against its source bundle and all CRCs; its log and exact archive hashes are preserved. The curation does not claim a second full ZIP traversal.

`SHA256SUMS` covers the large IPA and dSYM release assets, retained outside Git. `REPOSITORY-SHA256SUMS` covers this small curated directory. `source-evidence.json` fingerprints the original logs and identifies narrowed excerpts. Raw device IDs, process dumps and console logs remain local. Source snapshots match committed blobs.

Matched local cause/repair controls are in the adjacent `../mono-stack-repair` directory. Physical probes are separate gates; a completed probe does not establish gameplay or frame-rate quality. Release upload/digest verification has not yet been recorded here.
