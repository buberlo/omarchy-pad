# Build 4447 runtime follow-up — frozen

Hollow Knight's physical run PID 340 began at 11:57:56 UTC and was manually stopped at 12:13:22 UTC after 15 minutes 26 seconds. No visible game frame, gameplay or usable frame rate was established. It progressed past Mono, input and display setup and logged Apple M2 Venus/Mesa 26.2.4 as its Vulkan renderer, then reported an FMOD output-device initialization error (33) at 12:12:01.499 UTC. The operator verified the target and sent SIGKILL to prepare the next measured JIT repair. `GAME_EXIT=137` records that manual stop; it is not a spontaneous crash.

The signed/released build4447 record and Mono repair archive remain frozen and unchanged. This separate runtime record is now frozen at the manual-stop boundary. It makes no claim about build4448 or later audio diagnosis.

## Recorded progress

- 11:59:00 input module initialized; 11:59:01 display 0 identified as DesktopKit 1180×780. That display mode is not the requested or verified game swapchain extent.
- 12:01:41 first Vulkan instance-extension list; 12:03:18 physical device enumerated as `Virtio-GPU Venus (Apple M2 GPU)`.
- 12:03:26 Unity 6000.0.61f1 engine initialization; 12:03:33 Vulkan device-client creation begins; 12:04:29 device-extension enumeration progresses.
- 12:05:49 the Vulkan renderer/driver identity is logged as Apple M2 Venus/Mesa 26.2.4; 12:06:01 reports a PSO cache miss. The cache-miss line is not by itself a fatal error. The 12:08:40 capture has no later Unity phase line. The final captured phase is the FMOD output-device error at 12:12:01.499; error text alone does not identify the failing PulseAudio/ALSA operation.

The source reports are curated into `game-progress` with original line numbers and full original-file hashes in `source-evidence.json`. They retain the target process's CPU counters and necessary executable-map anchors, not complete process lists, file descriptors, thread dumps, or address maps. Mere library presence does not prove the renderer used it for a frame. The archive operation did not operate the device. The separately captured operator stop is preserved with its reason, target PID, time and result.

## Separate controls and metrics

The `cpu-accounting` audit verifies proc-stat CPU fields come from host thread run time. A local native calibration distinguishes two seconds sleeping (about 0.001 CPU seconds) from one second busy (about 0.88 CPU seconds). `cpu-timeline.json` computes PID 340's group CPU changes between the physical reports. These are real CPU consumption, not proof of useful progress: a busy loop also consumes CPU. Per-TID stat records repeat the group total and must not be summed. Reported normal proc states R/S are synthesized and do not establish blocked/running status.

The `stack-limit8` controls set the local soft and hard stack limits to 8 MiB before the unchanged Mono launcher, matching the launcher's tighter stack-limit situation. Mono returns a nonnull domain and exits 0 with no SDL (13.658s) and full embedded SDL using the local dummy backend (25.827s). This is supplemental local qualification; it is not an additional physical Wayland or game-rendering result.

The `graphics` document explains the existing software-WSI transport, how to identify the actual game view, and how to capture a bounded frame-delivery interval after visible rendering. Its parser separates full-window delivery rate from active-span cadence and excludes other views. The five synthetic parser cases are arithmetic/format controls only. No physical frame-log capture, game FPS, input response or playable scene has been established here.

The scripts in `launch-and-capture` preserve the exact launch and historical collection procedure; their PID/path values belong to this run. The launch was not restarted to add instrumentation. The final stop script prepared frame logging for the next build; that preparation does not qualify a frame from this run. The existing bridge's combined burst statistic is not a per-game frame-rate measurement.

## Bounded native CPU profile

Xcode Time Profiler sampled the physical app PID 21341 from 12:07:55.066 to 12:08:00.864 UTC while this same game run was active. Of 5,592 weighted CPU samples (5.592 seconds, with two missing backtraces), 95.60% self time was in `ss_invalidate_locked`; 95.69% inclusive time passed through `jit_invalidate_range`. The game-main and Unity graphics threads account for 85.57% and 12.91% of sampled time respectively. Dominant stacks repeatedly enter page/JIT invalidation, including user-memory writes from `clock_gettime`.

This identifies a native JIT invalidation CPU hotspot during a short startup interval. These percentages describe sampled CPU, not the whole 15-minute launch, elapsed-time shares, GPU load, frame rate or proof of a fix. `profiler/startup-profile-summary.json` preserves the selected measurements, bounded capture metadata, and hashes of local summary/XML/log sources. Raw traces, complete process inventories and device identifiers are excluded.
