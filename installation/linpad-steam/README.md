# Verified handoff: LinPad, Steam and FEX

Current runtime: **build 4445**, signed, installed and booted to the physical iPad desktop; protected-memory checks and all 24 prior exact-address fault cases pass. **Build 4444 is rejected** after two boot failures. See the [16 October performance update](PERFORMANCE-2026-10-16.md) for the fixes, persistent 512 MiB JIT cache, and enabled Steam Library Low Performance/Low Bandwidth settings. The rebuilt FEX passes all 18 allocator checks on this device. Build 4445 also completes a 1200-frame x86 Vulkan cube run with visible rotation; the elapsed time includes startup and is not an FPS result. Hollow Knight's native Linux depot is installed. The missing libGL library is repaired and load-tested on device; its next window error is unsupported Xwayland GLX. A native Wayland/Vulkan game test is pending; gameplay is not yet established. The [private build 4445 prerelease](https://github.com/buberlo/linpad-ipad-steam/releases/tag/ipad-steam-build-4445) is published with all seven asset hashes verified. The build 4441 sections below preserve the original sign-in milestone.

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
