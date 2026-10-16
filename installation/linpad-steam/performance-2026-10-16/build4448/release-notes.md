LinPad 1.4.0 build 4448 replaces the fallback JIT cache's full invalidation scan with an index by guest page. In the previous build4447 five-second CPU profile, that scan accounted for 95.60% of sampled CPU weight during Hollow Knight startup.

Built and signed from `d4ea14293dc2526e9a14de55f676adcdf88e4e31`; signed IPA and matching dSYM passed CRC/content verification, matching UUID `A5CFFE3B-C626-3F4A-A0CB-50CEC07154EB`. Both approved memory entitlements remain enabled. Installed and booted on the physical iPad with 512 MiB native JIT; physical ordinary and fallback self-modification checks both pass 200 rewrites.

The local actual-function harness passes 240,144 ASan/UBSan assertions. Real local ARM64 ordinary and fallback self-modification probes pass in normal JIT and tier 2. The guest fallback test source was committed separately as `e334e1b1` after the app's source pin and is not compiled into the app. See `../jit-ss-repair` for exact source and tests.

New-build game rendering, playability and steady-state performance remain pending. Prior Mono/GPU results are historical and are not counted as build4448 validation. Installed FEX/Mesa/EGL/Steam/game data are separate from the IPA. This is a development checkpoint.
