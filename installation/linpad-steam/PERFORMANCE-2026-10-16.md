# Performance follow-up — 16 October 2026

LinPad 1.5.0 build 4443 is signed, installed and running on the M2 iPad. It retains build 4442's idle PulseAudio fix and persistent 512 MiB native JIT arena, and repairs two faults blocking the FEX/Vulkan path. The physical x86 Vulkan cube now completes its 120-frame test without hanging. Visible cube rotation is verified; Hollow Knight playability remains under investigation.

This update supersedes build 4441 as the current runtime status and preserves build 4442's audio/memory results below. The earlier handoff remains the record of the first working Steam sign-in screen. The user subsequently signed in and downloaded Hollow Knight. Completing a download does not establish that the game launches or is playable; an observed Proton startup dialog also does not establish successful execution. The user approved switching to the Linux version once the direct Linux/Vulkan path is verified; the actual game path still needs qualification.

## Build 4443: x86 Vulkan progress

Source commit [`7238c32f`](https://github.com/buberlo/linpad-ipad-steam/commit/7238c32fe4de31cd8f81db13a623d83e57bbe372) fixes native JIT fault delivery and a GPU fence lock inversion. Invalid TLB entries are rejected before address-range arithmetic can wrap around, and the existing fault-recovery path cannot retry a null page as valid memory. GPU fence readiness reads atomic completion counters without taking the device lock, allowing completion and poll wakeups to finish without opposite lock ownership.

The minimal null-access probe previously retried 257 faults at the wrong address and exited 139. After repair it delivers the expected SIGSEGV address 0/code 1 and exits 0. The full host regression passes 24 exact-address SIGSEGV cases plus valid and crossing accesses in inline JIT, compact tier 1, tier 2, and gadget interpreter modes. The GPU lock regression exercises the production functions, passes the completion/readiness collision, and reproduces the former deadlock with its legacy negative control. [Before](performance-2026-10-16/evidence/null-fault-jit-before.txt) · [after](performance-2026-10-16/build4443/null-fault-jit-after.txt) · [GPU regression](performance-2026-10-16/build4443/fence-lock-regression.txt)

On the physical iPad, the build 4442 headless Vulkan probe passed, but its cube run stopped after GPU selection without recording completion. With build 4443, the headless probe passed in 8.85 seconds and the x86 Wayland cube completed 120 requested frames with exit 0 in 38.12 seconds. The report continued after the cube, confirming this run did not hang. These elapsed times include startup; they are not FPS measurements or a matched performance comparison. Two later captures establish visible cube rotation; gameplay is still pending. [Earlier stalled run](performance-2026-10-16/evidence/build4442-x86-headless-cube-stall.txt) · [build 4443 device result](performance-2026-10-16/build4443/device-gpu-validation.txt)

A further bounded run requested 1200 frames and exited 0 in 58.44 seconds. The two saved captures show different cube orientations, establishing visible motion on the iPad. The frame count and total time do not qualify steady-state FPS because initialization is included. [Run log](performance-2026-10-16/build4443/build4443-cube1200.txt) · [exact script](performance-2026-10-16/build4443/cube4443-visible.sh) · [first capture](performance-2026-10-16/build4443/build4443-motion-1.png) · [second capture](performance-2026-10-16/build4443/build4443-motion-2.png)

The new run identifies `Virtio-GPU Venus (Apple M2 GPU)`, Vulkan API 1.1. It also records the persisted cache value 512, 28.3G available storage, 700 MiB app footprint, 8192 MiB allowance, 7491 MiB headroom, enabled OOM protection and zero recorded OOM kills. The allowance is a dynamic runtime sample, not additional physical RAM.

Build 4443's developer signature passed deep/strict verification; both approved memory entitlements remain active. The app and dSYM share UUID `C8B4FFA5-ADC8-3C76-9FAF-1986637FC46F`. The IPA and dSYM ZIP passed complete ZIP CRC checks. The [manifest](performance-2026-10-16/build4443/build-manifest.json) preserves the original dirty build provenance and maps all ten source snapshots byte-for-byte to the subsequent commit. [Verification](performance-2026-10-16/build4443/VERIFICATION.md) · [checksums](performance-2026-10-16/build4443/SHA256SUMS) · [build command](performance-2026-10-16/build4443/build-summary.log)

The native Steam compatibility tool is installed, recognized, and selected for Hollow Knight in Steam's visible Compatibility dropdown. Launch options are `-force-vulkan -screen-width 960 -screen-height 540 -screen-fullscreen 0`; the Steam overlay is disabled for this game. The installed Linux depot and executable are now verified; the first direct game startup aborts before gameplay. [Tool status and reproduction](performance-2026-10-16/steam-compat/README.md) · [selection evidence](performance-2026-10-16/steam-compat/steam-native-tool-selected.png)

The [private build 4443 release](https://github.com/buberlo/linpad-ipad-steam/releases/tag/ipad-steam-build-4443) is published. All seven asset sizes and SHA-256 digests were checked against the local originals at 09:28:43 UTC; its tag resolves to documentation commit `83a2e42e6e29ecad0db634c66d77e7d42b66dabd`, and Actions remain disabled. [Release verification](performance-2026-10-16/evidence/build4443-release-verified.json)

## Native Linux game startup and missing Steam helper

At 09:23:04 UTC, the bounded installed-files verifier found Linux depot **367523** in `InstalledDepots` and confirmed `hollow_knight.x86_64` is an x86-64 Linux ELF; it exited 0. The saved sample showed 28.0G available. This verifies the Linux download, not a successful game start. [Native installation evidence](performance-2026-10-16/evidence/hollow-native-installed.txt)

The first direct start reached Unity/Mono and failed with `mono_pe_file_unmap: mono_file_unmap failed` and `Invalid argument (22)`, followed by a fatal signal report. The first 30 report lines are preserved without alteration; original raw reports remain separately saved in the local workspace and are identified by SHA-256. [Unity excerpt](performance-2026-10-16/evidence/hollow-unity-first-30-lines.txt) · [raw-report provenance](performance-2026-10-16/evidence/hollow-first-start-provenance.json)

A local conformance test identifies a matching FEX issue: mappings below 4 GiB use an allocator that rejects non-page-multiple `munmap` lengths, even for x86-64 programs. The Mac-hosted LinPad CLI passes all 18 native ARM64 checks; the current FEX binary fails 15; a temporary length-rounding diagnostic preload passes all 18. A subsequent physical game run with only that diagnostic preload no longer logged the earlier Mono unmap error, but LinPad itself crashed while inspecting a protected guest stack page. The source fix remains under local rebuild; gameplay is still unverified. [Source and regression evidence](performance-2026-10-16/fex-munmap-diagnostic/README.md)

Separately, Steam's ARM64 SDK launch helper was missing from the installed minimal client. A narrow repair package preserves Valve's unmodified 73,208-byte `steam-launch-wrapper`, invokes it through the existing private ARM64 glibc loader, and backs up differing existing files. Its SDK archive hash matches the retained official manifest, and the extracted executable matches the archive member byte-for-byte. The archive contains only this helper, two installation scripts, provenance, and host validation; the complete SDK ZIP and unrelated audio helper are excluded. [Helper repair and exact provenance](performance-2026-10-16/steam-helper-repair/README.md) The official helper initially returned 255 because its optional PipeWire namespace companion was absent. Supplying its supported `--no-audio-namespace` option fixed the standalone child test on the physical iPad; actual Steam Play success still requires a game test. [Physical helper and 24-case native fault validation](performance-2026-10-16/evidence/build4443-native-fault-validation.txt)

## Polling repair and measured effect

Source commit [`2dd9da3a`](https://github.com/buberlo/linpad-ipad-steam/commit/2dd9da3ac0c30db8fa94f8d243d76044fc84cf95) fixes idle FIFO and socket polling on Darwin. A poll entry requesting no events must not repeatedly wake on an unrequested readable FIFO. The new regression covers that condition while retaining requested level-triggered readability and pipe/socket hangup reporting.

The controlled idle audio measurements were:

| Measurement | Before repair | Build 4442 |
| --- | --- | --- |
| PulseAudio CPU ticks, user + system | 13749 + 123929 → 13788 + 124381 | 5 + 3 → 5 + 3 |
| Sample duration | 5.32 seconds | 5.03 seconds |
| CPU time relative to one core | 92.29% | 0% at the counters' resolution |
| Microphone pipe-source module | Loaded, suspended | Loaded |

The counters use 100 Hz (`fs/proc/pid.c`, `RU_TICKS`). The measurements establish elimination of the observed idle busy loop during those windows. They do not establish an overall Steam speed ratio, game FPS, audio playback quality, or continuous microphone recording quality. Aggregate Steam CPU snapshots have different download/progress states and are not a controlled before/after performance comparison.

Evidence: [baseline CPU excerpt](performance-2026-10-16/evidence/audio-baseline-cpu-excerpt.txt), [original idle audio state](performance-2026-10-16/evidence/audio-idle.txt), and [physical build 4442 probe](performance-2026-10-16/build4442/device-validation.txt). A temporary experiment unloaded the unused microphone module and stopped the loop; build 4442 fixes the polling path with the module restored. That workaround is historical, not the final configuration.

The physical FIFO probe passed with exit 0; empty or queued descriptors with no requested events slept for approximately 101–104 ms. Local epoll one-shot, nested epoll, pselect and eventfd regressions also passed. A separate named-FIFO last-writer HUP test still fails on both the old and repaired runtime; it is a documented pre-existing limitation, not a passing claim. [Local regression record](performance-2026-10-16/evidence/fifo-poll-regressions.txt)

## Memory and storage

Both approved Apple capabilities remain signed and provisioned: Increased Memory Limit and Extended Virtual Addressing. At the post-launch sample, LinPad reported 8192 MiB of app allowance, 704 MiB of footprint and 7487 MiB of headroom. OOM protection was enabled, with zero recorded kills. These are runtime values, not a promise that the same amount will remain available under another workload.

The native JIT startup log confirms a **512 MiB code arena**. Its prior automatic maximum was 256 MiB. The larger arena holds more translated code; it does not add physical RAM. The value was passed as a launch argument and written through `/proc/ish/.defaults/fastMode.codeCacheMB`. Persistence was then verified at 08:57:27 UTC on a second launch with no application arguments or `ISH_JIT_CACHE_MB` override: the single-key read returned numeric 512, and the native startup log again confirmed a 512 MiB arena. [Persistence evidence](performance-2026-10-16/build4442/cache-persistence-evidence.txt) The full preferences database and Steam authentication data are excluded from this archive. [JIT startup evidence](performance-2026-10-16/build4442/device-console-validation-excerpt.log)

Linux storage grows with the iPad's available storage. The root is a directory-backed fake filesystem, not a fixed-size VM disk: `fs/fake.c` assigns `.statfs = realfs_statfs`; `fs/real.c:621` calls `fstatvfs` on the root directory and returns the host's total/free/available blocks. There is no separate virtual disk size to increase in this implementation.

After the user's iPad cleanup, the saved `df -h /` sample showed **29.7G available** on the 106.0G host volume. That value can shrink as Steam downloads and unpacks files. The assistant's narrower cache cleanup removed only SHA-256-matched transfer archives already backed up on the Mac; it does not account for the user's larger storage recovery. Its before/after free-space numbers are also affected by the simultaneous download. [Storage sample](performance-2026-10-16/evidence/steam-clean-shutdown.txt) · [bounded archive cleanup](performance-2026-10-16/evidence/cache-cleanup.txt)

## Build, artifacts and reproduction

Build 4442 was produced locally with the existing Release/iPhoneOS/native-JIT configuration and the same bootstrap rootfs. Its developer signature passed deep/strict verification. Installing it over the same bundle ID preserved the installed Linux environment. Build 4441 remains available as a rollback.

The build happened before commit `2dd9da3a` was created. The manifest retains the original base commit and dirty working-tree record, and now maps the exact patch and regression source to that subsequent commit. The mapping was checked byte-for-byte; the build is not relabelled as a fresh build from a later checkout.

The signed IPA, matching dSYM ZIP and SHA-256 manifest were prepared under the original workspace's `work/performance/build4442/`. They are not embedded in Git. The [private build 4442 release](https://github.com/buberlo/linpad-ipad-steam/releases/tag/ipad-steam-build-4442) was checked at 09:13:43 UTC: all six assets were uploaded and matched local sizes and SHA-256 digests; the repository remained private and Actions remained disabled. [Release verification](performance-2026-10-16/evidence/build4442-release-verified.json) App and symbols share UUID `56F68939-0E1D-3F07-A975-B5615A2DA5B4`.

- [Build manifest](performance-2026-10-16/build4442/build-manifest.json)
- [Verification record](performance-2026-10-16/build4442/VERIFICATION.md)
- [Package and record checksums](performance-2026-10-16/build4442/SHA256SUMS)
- [Build command/result](performance-2026-10-16/build4442/build-summary.log)
- [Signing result](performance-2026-10-16/build4442/signing.log)
- [Curated diagnostic scripts](performance-2026-10-16/scripts/guest)

The checksum manifest names the package files kept outside Git. Download or copy those packages beside it before checking all entries. Paths in the copied verification record refer to the original build workspace; the repository equivalents are linked above. The existing [signing helper](scripts/host/sign-linpad.py) and [rebuild instructions](README.md#rebuild-and-re-sign) remain applicable. Session scripts retain the original local report-server address and require adaptation before reuse; do not run historical mitigation or upgrade scripts as background services.

No global speedup or playable-game claim is made. The remaining gate is the actual Linux game/runtime path, followed by visible rendering, input, audio and a sustained on-device performance test. No GitHub Actions, full preferences export, credentials, private signing keys, or Steam account files are included.
