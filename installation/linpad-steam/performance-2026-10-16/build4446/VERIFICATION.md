# LinPad 1.4.0 build 4446

Build 4446 is installed and launched on the physical iPad. Boot and the netlink/Unity SDL initialization fix pass. GPU presentation on this build, a rendered Hollow Knight frame, gameplay and steady-state performance remain pending; earlier build 4445 GPU results are historical controls.

Source: `72e1eef8b3125b39e7c852abf2d16bf7a1f82029`. The actual signed bundle reports version `1.4.0`, build `4446`. App and matching dSYM UUID: `7BF394C6-2F81-36E0-A997-90220832DB68`.

## Physical validation

- App installation succeeded; `device-install-evidence.json` preserves the result and raw-record hash without device/container identifiers.
- Native JIT starts with a 512 MiB code arena, fast mode on. The app's boot marker reports 0.18 seconds. This marker measures runtime boot, not the preceding manual JIT enablement or game startup.
- All 35 native netlink regression checks pass, including 101.1 ms idle poll, unique monitor ports, full group-mask readback, duplicate descriptor lifetime and existing route replies.
- Exact Unity embedded SDL haptic, joystick, timer/video and combined `0x3221` initialization all return 0. Video-only and combined initialization each retain one display. The identical v2 probe failed combined initialization on build 4445; see the adjacent `unity-sdl-repair` evidence archive.

Real libudev monitor creation succeeds. Its optional BPF filter enable still returns -22, and the direct UDEV diagnostic consequently exits 6. Unity ignores that later operation and all four SDL initialization calls pass. Residual SDL error strings are not failures when the associated call returns 0. No hotplug producer, BPF filter or dynamic membership support is claimed.

## Artifact and source verification

The local recorder verified build completion, deep/strict developer signature, both approved memory entitlements, matching app/dSYM UUIDs, complete ZIP CRCs and every packaged regular file against its copied bundle. `build-manifest.json` records executable, dSYM, rootfs and package SHA256 values. The root archive matches build 4445. Six committed source snapshots, their patch and the focused host regressions are included.

The fix adds an idle `NETLINK_KOBJECT_UEVENT` endpoint for libudev, retaining nonblocking/poll behavior, credentials-option state, group masks and port lifetime. Unsupported sends cannot be interpreted as route requests. The final local native probe also passes all 35 checks. Local embedded SDL after-fix results preceded the final group-mask-only correction; physical build 4446 now independently passes the exact combined initialization.

FEX, Mesa 26, EGL 25, Steam and installed games remain separate guest data, not a complete filesystem export in this IPA. The repository copy contains curated evidence and source text only. IPA, dSYM binaries, Payload, full device console/process inventories and credentials are excluded. `SHA256SUMS` indexes the local checkpoint including separate release assets; `REPOSITORY-SHA256SUMS` verifies only files included in the repository copy.

This is a boot/subsystem checkpoint. No rendered-game-frame, gameplay or FPS claim is made.
