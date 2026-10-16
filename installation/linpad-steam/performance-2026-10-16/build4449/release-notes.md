LinPad 1.4.0 build 4449 grows the fallback JIT PC lookup table for large FEX workloads and corrects Linux scheduler-priority bounds used by inherited thread attributes. It includes growth commit df8a7720 and scheduler-query commit d8fa5c1e.

Built and signed from `d8fa5c1ed8bbe324677408ae8768a2f243cde412`; signed IPA and matching dSYM passed CRC/content verification, matching UUID `AB1037BA-59E4-36E9-B2EB-A55908E809CB`. Both approved memory entitlements remain enabled. Installed and booted on the physical iPad with 512 MiB native JIT; physical ordinary and fallback self-modification checks both pass 200 rewrites; physical native and FEX scheduler-query/inherited-thread probes both pass 45 checks.

The local actual-function harness passes 338,461 ASan/UBSan assertions. Real combined-CLI ordinary/fallback ARM64 self-modification and exact Mono noSDL/fullSDLdummy controls pass. These are local functional controls; prior physical Mono/GPU results are not counted as new-build validation. See `../jit-ss-growth` for the exact source and tests.

The scheduler probe source is committed separately as `060b382324740adb355a0ab0ec06c2246e0812c7` after the app source pin; it is not compiled into the app. Its intentionally unchanged script is `device-scripts/sched4448-probe`.

The exact guarded Unity FMOD thread helper passes all four priority cases on this iPad build, compared with three failures on build4448. Workers retain SCHED_OTHER. This test creates no audio stream and does not qualify sound playback. The exact helper probe source and matched controls are in `../fmod-sched-repair`; its source hash is recorded separately in the manifest. This diagnostic probe is not compiled into the app.

New-build game rendering, sound, playability and steady-state performance remain pending. Installed FEX/Mesa/EGL/Steam/game data are separate from the IPA. This is a development checkpoint.
