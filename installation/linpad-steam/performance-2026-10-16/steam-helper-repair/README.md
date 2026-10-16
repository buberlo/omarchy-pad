# Missing Steam ARM64 SDK launch helper

The physical device task confirmed `/root/.steam/sdkarm64` and
`steamrtarm64/steam-launch-wrapper` were absent. This is a missing SDK package,
not proof of a missing ELF interpreter. The locally retained minimal client
archive `bins_linuxarm64_linuxarm64` does not contain the helper.

Valve's separately published `bins_sdk_linuxarm64_linuxarm64` archive contains
`linuxarm64/steam-launch-wrapper`. Its downloaded SHA-256 matches the recorded
official ARM64 public-beta manifest. Only that unmodified official executable is
included in the small repair payload; provenance is in `PROVENANCE.json`.

The executable is AArch64 ELF, requests `/lib/ld-linux-aarch64.so.1`, and needs
`libc.so.6` plus the loader. Its highest linked glibc version is 2.38; the existing
LinPad private ARM64 glibc is 2.41. The helper also contains optional systemd
integration. These static facts do not establish successful device execution.

## Installation

Extract the repair in a dedicated temporary directory inside LinPad, then run
`sh install.sh`. It places the original executable at
`/opt/steam-host/libexec/linpad-steam/steam-launch-wrapper.real` and a shell entry
at `$HOME/.steam/sdkarm64/steam-launch-wrapper`. The shell invokes the existing
private ARM64 glibc loader without changing cwd or command arguments. Existing
different regular files are copied beside themselves with a timestamped
`.before-linpad-...` suffix before replacement; final symlinks are refused.

No Steam account data, configuration, downloads, or SDK steamclient library is
read or changed. Steam is not restarted. A separately observed bare-command
`steam-runtime-launcher-service: not found` is outside this repair's scope.

Overrides: `LINPAD_STEAM_SDK_DIR` and `LINPAD_STEAM_HELPER_DIR` select absolute
installation paths. When overriding the helper directory, keep that environment
variable for launching too. The shell also accepts `LINPAD_STEAM_ARM64_LIBDIR`
and `LINPAD_STEAM_ARM64_LOADER` for explicit runtime diagnostics.

After installation, a bounded `steam-launch-wrapper -- /bin/true` probe and then
the actual Steam Play path must be checked by the main device task. To roll back,
restore the printed backups to their corresponding paths. For files that were
newly created, remove only those two installed files; other SDK contents are
outside this repair.

## Archived evidence

The repository copy contains only the unmodified 73,208-byte SDK helper, its narrow installer/shell entry, static and mock-install validation, and provenance. The official manifest package entry is preserved in `official-manifest-excerpt.vdf`; the complete local SDK ZIP is intentionally outside this repository copy. Archive/member equality and all recorded SHA-256 values were rechecked before copying.

`VALIDATION.txt` describes host-side checks. Device installation/probe results will be added separately when the active device task finishes them; no helper execution or game startup is claimed from those host checks.

## Actual helper execution correction

The explicit private-loader invocation makes `/proc/self/exe` identify that
loader. The official helper's default optional PipeWire namespace then searched
for an absent sibling `pw-audio-namespace`, returning 255. Its supported
`--no-audio-namespace` option avoids that optional isolation; the child command
and PulseAudio game sound are unaffected. The shell entry now supplies this
option before the original arguments.

The corrected helper passed `-- /bin/true` on physical iPad build4443 at
17:40 local on 2026-10-16 (HELPER_SMOKE_PASS). The actual Steam Play path still
requires a successful game test. Local diagnostic traces are retained beside
this document.
