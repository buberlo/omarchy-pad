# LinPad 1.5.0 build 4442 — device verification

Build 4442 fixes a FIFO polling condition that caused unnecessary wakeups. It was built locally, signed with the existing development profile, installed over build 4441 and executed on the M2 iPad. The installed Linux environment was retained. Hollow Knight playability and overall graphics performance remain unqualified; FEX/game investigation is ongoing.

## Verified on 2026-10-16 at 08:46 UTC

- Native JIT started through StikDebug using a 512 MiB code arena, confirmed in the device console. This is the translated-code cache; it is not an additional 512 MiB RAM allowance.
- `/proc/ish/memory` reported an 8192 MiB app allowance, 704 MiB footprint and 7487 MiB headroom at the sample. These values are a runtime snapshot, not a guaranteed future allocation. OOM protection remained enabled with zero recorded kills.
- The FIFO idle-poll probe passed on-device with exit 0. Cases requesting no events waited approximately 101–104 ms; requested readability remained level-triggered; pipe and socket HUP were still reported without requested events.
- PulseAudio retained its microphone pipe-source module. User/system CPU counters stayed at 5/3 ticks throughout a 5.03-second idle sample, demonstrating removal of the observed idle busy loop during that window.
- Both approved memory entitlements are in the executable and provisioning profile. Developer signing and `codesign --verify --deep --strict` passed.

The 512 MiB value was applied through a launch argument and written through the single-key proc preferences interface. Persistence was verified at 08:57:27 UTC after a second launch with no application arguments or JIT-cache environment override: the single-key proc read returned numeric 512 and the native startup log again reported a 512 MiB arena. See `cache-persistence-evidence.txt`. The existing named-FIFO last-writer HUP limitation still fails its separate diagnostic; the successful device probe above does not claim that limitation is fixed.

## Artifacts and provenance

- `LinPad-1.5.0-build4442-Konrad.ipa`: signed app; contains the bundled bootstrap rootfs, not the current iPad Linux filesystem or Steam account data.
- `LinPad-1.5.0-build4442-dSYM.zip`: matching symbols.
- `SHA256SUMS`: checksums of both packages and the fixed verification files.
- `build-manifest.json`: source revision/patch hashes, binary/rootfs hashes, signing and device measurements.
- `source-changes.patch`: exact tracked source changes used for this build. The regression probe source is included as `fifo-idle-poll-probe.c`, with its hash in the manifest.
- `device-validation.txt`: frozen device test report.
- `device-console-validation-excerpt.log`: native-JIT startup evidence.
- `cache-persistence-evidence.txt`: retained preference and active arena after relaunch without a cache override.

App and dSYM UUID: `56F68939-0E1D-3F07-A975-B5615A2DA5B4`.

Local build/sign logs are `../build4442-build.log` and `../build4442-signing.log`. The full device console remains `device-console.log`; it may continue collecting later investigation and is intentionally not treated as a fixed snapshot here. Build 4441's rollback IPA remains unchanged, SHA256 `2f2c3aa2a42807e73687a5d162cb41e72071993163ba7c2c2887f33ba301cef7`.

Packages contain normal development certificates/provisioning metadata but no private signing keys or exported account credentials. No GitHub Actions were used. This packaging step does not commit or upload the artifacts.
