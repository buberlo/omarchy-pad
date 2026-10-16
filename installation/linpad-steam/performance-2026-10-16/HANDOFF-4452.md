# Latest handoff: user takes over

The user requested stopping after documentation and commit/push. Hollow Knight was subsequently launched and its main menu observed; the user reported smooth operation. Enter works on the connected Magic Keyboard; the final user report says the other keys do not. No keyboard fix, normal gameplay acceptance or numerical performance result is claimed. Recording is now the user's responsibility; our attempted recording produced no video and was stopped. Start the existing installed game using `sh /tmp/hollow4452-egl` in LinPad's Terminal. See build4452-menu-followup/README.md for exact current evidence and instructions. Do not resume implementation or device tests until asked.

The remainder records the earlier build checkpoint and future investigation possibilities, not authorization to continue now.

# Build 4452 handoff

Installed and signed LinPad 1.4.0 build 4452 passed its physical ARM64 memory regression: 19,151 checks, zero failures, both existing SMC probes passed. Source commit bdc6ce6e935f3bcf28d78e33b86496c8a65edf0b contains the native LRCPC2 fix. Code, tests and installation documentation were pushed at 283796b011f199f6ed850a242736bd61282de4a7. The private build 4452 release contains the signed IPA, matching dSYM, manifest, SHA256SUMS and verification document; all server digests match.

No new Hollow Knight game run has occurred on 4452. Smooth audio, playable gameplay and Steam-to-game production launch remain open. The native fix replaces the LDAPUR/STLUR gadget fallback identified in all retained 4451 samples. Default FEX feature detection must stay in use; do not enable the rejected disablelrcpc2 override.

The initial verified host 21791 was manually restarted after the device tests while preparing fresh frame logging. The new own host 21814 successfully enabled StikDebug JIT with the same 512 MiB arena and diagnostic environment. No game is running. A remotely typed frame-hook download was truncated and failed with a bad-address message; the hook was not installed. Do not assume the compositor currently logs frames. Remote typing needs completion verification before Return and before stopping a process.

Next: create and verify /etc/ishwl/session.d/99-hollow-frame-capture.sh with ISHWL_FRAMELOG=/tmp/hollow4452-frames.txt; only then restart the own LinPad host and verify the log opens. Do this while no game is running. Remove the temporary hook after startup, keeping the open log. The earlier saved prepare4452, hollow4452-egl, short, frame-capture, menu-cadence and ss4452-probe helpers are already under /tmp. Native JIT and all low-level device gates have passed; repeat only if the build/runtime changes.

Run /tmp/hollow4452-egl through the existing FEX/Wayland/EGL/Venus route. Capture fresh game PID and bounded /proc/ish/jit-ss observations. Compare the foreground German title menu at the same actual 659x540 with the frozen 4451 baseline (212 deliveries in 30 seconds; 7.0667/s). No timing claim may use only startup counters. Continue to real movement/combat and audio. Then integrate the proven route into Steam and verify Steam-to-game launch. Existing slot 1 is the user's save; slot 2 was created for testing. DeviceHub keyboard capture must stay off. Respect current shared-device ownership before any later device action.

Goal remains active: make Hollow Knight run well on the iPad through LinPad and Steam. The completed build/install/backup checkpoint does not satisfy gameplay acceptance.
