# Verified handoff: LinPad, Steam and FEX

Current runtime checkpoint: **LinPad 1.4.0 build 4451**, signed, installed and booted on the physical M2 iPad with both approved memory entitlements and 512 MiB native JIT. Both physical self-modifying-code probes pass 200 rewrites, and the opt-in fallback diagnostic self-test passes. Hollow Knight's German main menu renders. A continuously foregrounded 30-second capture delivered 212 game-view frames (7.0667/s, 659×540); this measures compositor delivery, not gameplay or display FPS. The user confirmed audible but severely stuttering menu music on build 4450. Smooth audio and normal gameplay remain unqualified. [Build 4451 verification](performance-2026-10-16/build4451/VERIFICATION.md)

The actual game startup diagnostic identifies LDAPUR/STLUR in all 1,223 retained sample records. A separate launch using only `FEX_HOSTFEATURES=disablelrcpc2` exits 139 before Unity initialization; it is not a recommended setting. The baseline was deliberately stopped for this test. [Controlled launch experiment](performance-2026-10-16/build4451-lrcpc2-control/README.md)

Build 4450 adds a per-context fallback translation cache to reduce the shared lookup lock. Its late-loading profile still attributes 40.28% of sampled CPU time to the shared lookup path, including 33.26% in mutex wait/wake below it. Further diagnosis is required; the earlier build's menu capture had incomplete foreground qualification and is not a controlled performance baseline. [Cache implementation and controls](performance-2026-10-16/jit-ss-context-cache/README.md). Build 4449's scheduler/inherited-thread checks passed 45/45 in native ARM64 and FEX, and the exact Unity audio-thread helper passed 4/4. [Build 4449 verification](performance-2026-10-16/build4449/VERIFICATION.md)

Build 4447's CPU profile attributed 95.60% of sampled startup CPU time to scanning the entire fallback JIT cache on page invalidation. Build 4448 indexes those entries by guest page. Exact source, before-profile analysis, sanitizer checks and real ARM64 code-rewrite controls are preserved in [JIT invalidation repair](performance-2026-10-16/jit-ss-repair/README.md). **Build 4449 subsequently displayed Hollow Knight’s language-selection UI on the physical iPad, with working mouse selection. Gameplay and steady-state performance remain unverified.**


Build 4449 also grows the fallback PC lookup table: the fixed 256-bucket table became the next measured bottleneck after the page-index repair. Its independent review, actual-function sanitizer tests and unchanged real-code controls are in [JIT lookup growth](performance-2026-10-16/jit-ss-growth/README.md). Linux scheduler min/max queries now return the correct policy bounds, allowing Unity to create ordinary inherited audio threads; the exact helper changes from three failures to four passing priority cases on the iPad. [FMOD thread repair](performance-2026-10-16/fmod-sched-repair/README.md). Actual menu presentation and user-reported audible sound are now established; smooth playback and gameplay remain separate gates.

Two earlier startup defects are also repaired: build 4446 added the uevent monitor support needed to keep SDL video initialized; build 4447 corrected `/proc/self/maps` endpoints so Mono guards its own stack instead of live FEX metadata. [Unity SDL repair](performance-2026-10-16/unity-sdl-repair/README.md), [Mono stack repair](performance-2026-10-16/mono-stack-repair/README.md). Their physical results are historical gates, not new-build gameplay evidence.

Hollow Knight's native Linux depot is installed. The separate [EGL runtime](performance-2026-10-16/egl-repair/README.md) provides auxiliary SDL/Wayland initialization; explicit Vulkan selects Mesa 26 Venus on Apple M2. FEX/Mesa/EGL, Steam and game data are separate guest additions, not a complete filesystem export in the IPA. Linux storage is directory-backed and uses the iPad's available filesystem space; the build 4447 physical report shows about 28 GiB available. [Physical storage and memory report](performance-2026-10-16/build4447/maps4447-device-pass.txt)

The [private build 4447 prerelease](https://github.com/buberlo/linpad-ipad-steam/releases/tag/ipad-steam-build-4447) is published with all five asset sizes and server SHA-256 digests verified. The [private build 4448 prerelease](https://github.com/buberlo/linpad-ipad-steam/releases/tag/ipad-steam-build-4448) is also published; all five server asset sizes and SHA-256 digests match the verified local files. The [private build 4449 prerelease](https://github.com/buberlo/linpad-ipad-steam/releases/tag/ipad-steam-build-4449) is published with all five server asset sizes and SHA-256 digests verified. The [private build 4450 prerelease](https://github.com/buberlo/linpad-ipad-steam/releases/tag/ipad-steam-build-4450) is published with all five server asset sizes and SHA-256 digests verified. The sections below preserve the original build 4441 sign-in handoff and historical limitations; use current verification records for subsequent results. The older GPU cube is a separate smoke test, not game performance.

## Build 4441 sign-in milestone

The physical iPad Air 13-inch M2 ran signed LinPad 1.5.0 build 4441 and displayed
Steam's account sign-in screen. The assistant entered no Steam credentials.
Steam was left open for the user. Account sign-in, downloads, games and gameplay
are not qualified. The final app has both user-approved Apple memory capabilities:
Increased Memory Limit and Extended Virtual Addressing. The signing profile
expires on 16 October 2027; continued use still depends on device/developer state.

The `steam` command and Steam application-menu entry invoke the verified local
Xvnc/GLX launcher. Its previous launcher is preserved on the iPad as
`/usr/local/bin/steam.linpad-original`. This is on-device rendering, not a Mac
stream. llvmpipe supplies software GLX; the separate Vulkan cube smoke test used
the LinPad GPU path. These are distinct validation results.

## Runtime repairs, in order

| Commit | Change |
| --- | --- |
| `7b7bf74` | MIDR_EL1, relative descriptor access, eventfd semaphore, null pselect masks, one-shot epoll, proc allocation guard, build/signing integration |
| `feea659` | Rearming a ready inner epoll member wakes the outer epoll set |
| `ecc8dd9` | Protect proc fd directory enumeration against process exit |
| `7a0d17d` | Expose guest TCP/UDP IPv4/IPv6 socket endpoints and consistent inode IDs |
| `140e149` | Enumerate process leaders, retain direct TID lookup; correct IPv6 proc header |
| `b54295b` | Report process parent IDs rather than worker TIDs in getppid and proc stat/status |

The fd-exit regression crashed the old local runtime and passed after repair.
The parent-ID regression failed before and passed after repair. Build 4441's
physical regression run passed parent IDs, thread enumeration/direct lookup,
TCP/UDP IPv4/IPv6 metadata, and 300 exit-race rounds with 2,937 directory scans.
Earlier epoll, eventfd and pselect device runs are preserved in evidence.

Steam uses lsof to identify both ends of local browser IPC. The missing network
proc files, thread/process enumeration and parent IDs prevented reliable
ownership/ancestry checks. The final parent-ID correction was followed by the
actual sign-in screen. See the dated before/after traces and device evidence.

## FEX and Madeira

FEX upstream revision `5285020de0b30dbdae7b44d0d83541af2d13d002` was current
when checked; FEX-static release `5285020de` matches it. Cortex-A72, Apple M1
and Cortex-A76 static variants all passed hello, threads/mutexes, SSE2,
generated executable x86 code and signals on the iPad with `ulimit -s 8192`
and the memory entitlements. No full x86 rootfs/application/game was qualified.
The ARM64 Steam client itself runs without FEX translation.

Madeira source `279fd0f83170425db002fe25d6bd62f6355f497d` and its iOS FEX pin
`f59cab95659e8add0c88f0c76ec1cf483c536dbf` were inspected. Madeira's CPU probe
variable is Windows-specific; its Dock bypasses Chromium using Wine/client
interfaces; its standalone Linux FEX bridge only handles small write/exit probes.
None is a drop-in Linux Steam fix. Madeira's LLDB BRK protocol matches LinPad,
but the attempted direct LLDB startup did not prepare its executable pool;
StikDebug was the working JIT route. No FEX source changes were made.

## Repository and release layout

Source and all six runtime commits are in this repository. `scripts/guest` keeps
the exact staged scripts, including obsolete experiments; **use steam-local,
steam-verified-wrapper and fex-launcher as the final launchers**. `try4441` is a
bounded historical diagnostic, not a startup service. `scripts/host` includes
the original build/signing/download helpers. Session scripts contain the old
workspace, LAN address and device IDs; adapt them before reuse.

`diagnostics` preserves local/device traces, build/install/signing logs and the
LinPad native crash reports. `evidence` includes both final and earlier results.
Older files such as fex-ipad-tests.txt describe early failures and are superseded
by fex-ipad-current-builds.txt and the final status document. The final sign-in
image is steam-ipad-login-4441.jpg.

Private release `ipad-steam-build-4441` contains:

- Final signed build 4441 IPA and original signed 1.5.0 rollback IPA.
- The complete staged Steam/FEX transfer directory, including binaries, isolated
  dependency payloads, experimental scripts and official client payload.
- The iOS GPU prefix and third-party notices used in the local build.
- Matching final dSYM symbols, for native crash diagnosis.
- SHA-256 manifest for every release asset.

This is a source/build/transfer archive, **not a byte-for-byte export of the iPad
Linux filesystem**. Installed Alpine Xwayland dependencies under /opt/steam-x11,
util-linux/taskset under /opt/steam-utils, system edits and user files must be
preserved on the existing device or recreated for a fresh root. The staged
payload archive plus the scripts document that setup, but a fresh-root restore
has not been independently tested. Linux account/config data is intentionally
absent, so this backup does not upload Steam authentication material.

## Rebuild and re-sign

Use Xcode locally, initialize the recorded Git submodules, and install ninja,
pkg-config plus the Python versions in requirements-host.txt. The build used
meson 1.12.1, mako 1.4.3 and ziglang 0.17.0. GPU dependencies can be rebuilt with
`gpu/build-third-party.sh ios` or restored from the private GPU release asset.
Generated pkg-config/cross files contain machine paths and may need regeneration.

Extract `Payload/iSH ARM64.app/root.tar.gz` from a saved IPA to a local file.
The successful build command was:

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
ZIG_GLOBAL_CACHE_DIR="$LOCAL_WORK/zig-cache" \
PATH="$LOCAL_WORK/compiler-bin:$LOCAL_WORK/build-tools/bin:/opt/homebrew/bin:$PATH" \
xcodebuild -project iSH.xcodeproj -target iSH-ARM64 -configuration Release \
-sdk iphoneos SYMROOT="$LOCAL_WORK/ios-build" OBJROOT="$LOCAL_WORK/ios-obj" \
IPHONEOS_DEPLOYMENT_TARGET=17.0 ISH_JIT_BUILD=enabled CODE_SIGNING_ALLOWED=NO \
ROOTFS_ARCHIVE="$LOCAL_WORK/root.tar.gz" build
```

`LOCAL_WORK` is a path chosen for the local rebuild. The compiler wrapper routes
AArch64 Linux cross-compiles through the installed Zig cc and native compilation
through Apple's clang; the session wrapper is recorded beside the host scripts.
The saved sign-linpad.py demonstrates the exact signing process, but its profile,
certificate identity and device assertion are session-specific. Adapt those
inputs to the developer profile and keychain on the restoring Mac. Both memory
capabilities must be present in the provisioning profile, not just entitlements.
No private key, .p12/.p8, developer password or pairing record was uploaded.

Installing the saved final IPA on its provisioned iPad preserves existing app data
when its bundle ID is unchanged. It does not restore Steam into a new Linux root.
Fast mode requires StikDebug's universal.js handoff after an app restart. The
original working URL included bundle ID, current PID and script-name=universal.js.

## Device and input coordination

Respect the shared device lease at installation/ipad-access.json in the Madeira
workspace before running tests. Its historical helper is recorded, but do not
reuse an old token. The reservation and temporary HTTP transfer server were
released/stopped at the end of the device test.

Do not enable Device Hub keyboard capture: it intercepted the user's Mac keyboard
while Device Hub was unresponsive. Force-closing only Device Hub released it;
reopening the viewer with capture disabled restored remote input. Keep capture
disabled. All final tests ran without it. No recurring automation was created.

## Remaining limits

The Steam packet-pipe shim maps its small packet notifications to SOCK_SEQPACKET;
it is not a full O_DIRECT pipe implementation. Software GLX performance is not
GPU game performance. Openbox was downloaded/prepared on the Mac but never needed
or installed on the iPad. Chromium debug ports were not enabled. Steam sign-in
and any later gaming tests require new evidence before claiming success.

## Sources

- https://github.com/fspecii/LinPad
- https://github.com/FEX-Emu/FEX/releases/tag/FEX-2610
- https://github.com/PeronGH/FEX-static/releases/tag/5285020de
- https://github.com/willfaust/Madeira/commit/279fd0f83170425db002fe25d6bd62f6355f497d
- https://github.com/ValveSoftware/steam-for-linux/issues/11822
- https://github.com/lsof-org/lsof/blob/master/lib/dialects/linux/dsock.c
