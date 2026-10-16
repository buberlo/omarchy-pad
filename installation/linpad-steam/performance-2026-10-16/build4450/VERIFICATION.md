# Build 4450 verification

LinPad 1.4.0 build 4450 adds a 256-entry persistent fallback-instruction cache per JIT context to reduce repeated shared lookup locking. App source is `f93f14493b4d1b38f5e42b97613237891c1e9b48`. Documentation was committed separately as 0afc2925 and is not the app source pin.

The signed IPA and matching dSYM passed the original recorder's full ZIP CRC and bundle-content checks, with matching UUID `5FB3CB79-8481-3307-97C7-5EEFD91C69D2`. Both approved memory entitlements remain enabled. Installed and booted on the physical iPad with 512 MiB native JIT. Both physical ordinary and actual fallback self-modification tests pass 200 rewrites.

Independent source review passed. The actual-function fixture passes 438495 ASan/UBSan assertions, including stale-pointer generation rejection and one-shot exclusion. Unchanged real local ordinary/fallback SMC and exact default-FEX Mono noSDL/fullSDLdummy controls pass; their source hashes and CLI identity are archived separately in `../jit-ss-context-cache`. Synthetic lock counts and local timings are not physical game performance.

The preceding build 4449 run was manually sent SIGTERM then SIGKILL at 13:10:37–40 UTC to install this candidate. It had 5633 aggregate compositor rows and reached language/title-menu/setup stages; this is not a gameplay or aggregate FPS result. Its unchanged screenshots and qualified cadence samples remain in `../build4449-visible-menu`. None are attributed to build 4450.

The new build displays the German Hollow Knight title menu, first observed around 13:20:48 UTC after the 13:13:59 launch. The unchanged screenshot is included. This observation time is not a measured first-frame latency. Unity requested 960×540 initially, then reset the swapchain to 659×540 at 13:15:54.300; the bounded capture retained the same view 1 and backing-file identity at that size.

From 13:21:03 to 13:21:33 UTC, the static title-menu view delivered 210 compositor rows over the full 30 seconds: **7.0 composited deliveries/second**. Sequences 3183–3392 are contiguous, median interval 142 ms, 95th percentile 149 ms, maximum 184 ms. The parent confirmed the menu foreground before capture and checked the screenshot at 13:21:34, with no window switch during the measured interval. An operator-added 15-second delay preceded the wrapper's recorded five-second delay. This measures game-view delivery through the compositor, not GPU time, independent display refresh or gameplay FPS. The report and parser preserve the full-window count, separate from its active-span rate.

On 2026-10-16 the user reports audible menu music that stutters badly. This is user-reported evidence, without an audio recording or instrumented playback measurement. Normal gameplay and steady-state playable performance remain pending.

The earlier 13:17:31–37 loading profile still attributes 33.26% of weighted CPU samples to mutex wait/wake under shared SS lookup, and 13.26% to fallback CPU-state copying. Its exact aggregates and source analysis are included under `profile`. This is a different phase from the menu capture and does not establish why cache misses occur.

This checkpoint is frozen. Installed FEX/Mesa/EGL/Steam/game data are separate from the IPA. Prior 4449 scheduler/helper results are retained in that build's archive and are not relabeled as 4450 retests. The saved DE language preference carries over from 4449, and its prior menu capture had uncertain foreground state; neither startup nor menu cadence supports a matched causal performance comparison.

The immutable original artifact record is copied as initial-build-manifest.json. Its recorder was not modified or rerun. This curator rechecks executable/DWARF hashes and exact source snapshots against committed blobs; it preserves the original archive verification log and digest/size records without claiming a second full ZIP traversal.

SHA256SUMS covers the large release assets kept outside Git. REPOSITORY-SHA256SUMS covers this lightweight folder. source-evidence.json fingerprints original logs and identifies narrowed excerpts. The predecessor 4449 evidence is explicitly separate from the physical 4450 gates. No release upload/digest verification is claimed here.

The parent owns further device qualification and publication. Subsequent gameplay or source work belongs in a separate follow-up record. Later opt-in diagnostics are separate source work and are not attributed to this build.
