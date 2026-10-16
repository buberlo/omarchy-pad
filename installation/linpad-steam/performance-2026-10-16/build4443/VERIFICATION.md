# LinPad 1.5.0 build 4443

Built and signed locally on 2026-10-16. The signed app was installed successfully and launched on the iPad. Native JIT is active with a 512 MiB code arena. The physical Vulkan probe and 120-frame x86 Wayland cube completed without a hang. Two physical-device captures show cube rotation; Hollow Knight gameplay validation remains pending.

## Changes

- Native ARM64 JIT rejects the invalid TLB tag before address-range arithmetic and prevents invalid null-page recovery. This restores precise guest SIGSEGV delivery for the Mesa null-access probe.
- GPU fence readiness reads atomic completion counters without taking the device lock. This removes the reproduced inversion between the poll wakeup path and GPU completion path.

## Verified locally

- Release iPhoneOS build succeeded with JIT enabled and deployment target 17.0.
- Developer signing succeeded; `codesign --verify --deep --strict` exited 0.
- Increased Memory Limit and Extended Virtual Addressing entitlements are both true in the signed executable.
- Executable and dSYM UUID match: `C8B4FFA5-ADC8-3C76-9FAF-1986637FC46F` (arm64).
- All ten source snapshot hashes matched the live source after compilation.
- JIT fault regression passed 24 exact-address SIGSEGV cases plus valid/crossing accesses in inline JIT, compact tier 1, tier 2, and gadget interpreter modes. Logs are beside this document.
- GPU regression passed readiness/completion collision and reproduced the legacy deadlock with its negative control. The saved log records the earlier host test; it is not a device result.

The existing 512 MiB JIT cache preference was verified persistent during a normal build 4442 relaunch, with no command-line cache override. This build does not alter preferences. The embedded root archive is byte-identical to build 4442; existing installed Linux data is not part of this artifact.

## Provenance

`build-manifest.json` records the original dirty source base, exact file hashes, executable/rootfs hashes, and symbols. `source-changes.patch` contains tracked changes; `source-snapshot/` also includes the two new regression tests. Build 4442 artifacts remain intact for rollback. The signed IPA and matching dSYM ZIP are packaged; both archives passed full ZIP CRC verification. Upload is awaiting the main task's final device validation decision.

Build log: `../build4443-build.log`. Signing log: `../build4443-signing.log`.

## Device launch evidence

`install.json` records the successful install of `com.konradkern.linpad`. `device-launch-evidence.txt` freezes the launch, active JIT, 512 MiB arena, and boot lines from the live device console. This confirms startup; it does not establish graphics presentation or gameplay.

## Physical Vulkan validation

`device-gpu-validation.txt` records the 2026-10-16 physical-device run:

- Headless Vulkan creation, queue, idle, and destruction probe passed with exit 0 in 8.85 seconds; renderer was Virtio-GPU Venus (Apple M2 GPU), API 1.1.
- The x86 Wayland cube requested 120 frames and returned exit 0 after 38.12 seconds. The report continued after completion, so the earlier hang did not reproduce. This duration includes startup and is not an FPS measurement.
- The persisted cache preference still read 512; the filesystem reported 28.3 GiB available. Memory-pressure handling remained enabled with zero recorded OOM kills.

A further bounded run requested 1200 frames and exited 0 in 58.44 seconds (`build4443-cube1200.txt`, exact script `cube4443-visible.sh`). Two captures, `build4443-motion-1.png` and `build4443-motion-2.png`, show different cube orientations and establish visible motion. Total elapsed time includes startup and does not qualify steady-state FPS. Hollow Knight gameplay remains unqualified.

The ten captured source files were subsequently committed as `7238c32fe4de31cd8f81db13a623d83e57bbe372`. Each committed blob matches its build-time snapshot SHA-256; the manifest retains the original pre-commit base and dirty state.
