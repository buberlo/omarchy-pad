# LinPad iPad: Steam and FEX

Private archive of the LinPad work verified on 16 October 2026.

**Current device checkpoint:** the iPad runs the parallel LinPad Steam work — desktop build 4478
and the second app "LinPad Steam" build 4480 (`com.konradkern.linpad.steam`, same app group, one
shared Linux system) — plus the two fixes recorded here, both of which are in that lineage:
the keyboard routing fix (`b432013e`, an ancestor of the Steam commit) and the deployed
compositor fix (`bd6e9881`).

**The keyboard works, and it is now proven by the compositor's own log.** `ishwl` built from
`bd6e9881` was installed on the device and the session restarted onto it; in one play session it
logged printable keys as key events for the first time — X 116×, Z 30×, Y 14×, Space 44× — where
the system compositor had produced no key event at all for them. The wait that made a released key
keep moving is measured gone: text after a bare key press arrives in **0.0 ms** against **260.6 ms**
with the previous revision, while an edit key still waits as intended. [Deployment and key log](installation/linpad-steam/performance-2026-10-16/build4453-keyboard/ishwl-deployed/README.md),
[headless before/after measurement](installation/linpad-steam/performance-2026-10-16/build4453-keyboard/ishwl/README.md).

**Steam now starts Hollow Knight.** Every launch used to die in the same second because Steam's own
`reaper` calls a `prctl()` iSH does not implement, and the compatibility tool defaulted to the wrong
translator and display mode for this device. Both are fixed and the Play path is verified up to a
running game, a fresh Unity log and 1 801 frame deliveries in 30 s on the Venus renderer.
[Steam route, device changes and rollback](installation/linpad-steam/performance-2026-10-16/steam-game-route/README.md).

**Still open: nobody has confirmed gameplay and audio by hand** with either route (movement, combat,
menus, sound), and frame *deliveries* are not an FPS measurement. [Current handoff and next steps](installation/linpad-steam/performance-2026-10-16/HANDOFF-4453.md).

Build 4452 remains the checkpoint for the JIT work: it adds native translation for the LDAPUR/STLUR memory instructions identified as the dominant slow fallback, and its physical ARM64 regression passes19151 checks, including8190 memory cases,200 code rewrites and10000 thread exchanges. [Build4452 verification](installation/linpad-steam/performance-2026-10-16/build4452/VERIFICATION.md). [Private signed build and symbols](https://github.com/buberlo/linpad-ipad-steam/releases/tag/ipad-steam-build-4452).

To start on the existing iPad installation, open LinPad's Terminal and run:

```sh
sh /tmp/hollow4452-egl
```

Build 4441 originally established Steam ARM64 sign-in, static FEX probes and the Vulkan cube smoke test; those historical records remain below.

- [Full handoff and recovery notes](installation/linpad-steam/README.md)
- [Steam sign-in evidence](installation/linpad-steam/evidence/steam-ipad-login-4441.jpg)
- [Latest device regression results](installation/linpad-steam/evidence/linpad-4441-runtime-checks.txt)
- [Madeira and latest FEX assessment](installation/linpad-steam/evidence/madeira-fex-reuse.txt)
- [Regression source](tests/manual/steam-compat/README.md)
- [Private release assets](https://github.com/buberlo/linpad-ipad-steam/releases/tag/ipad-steam-build-4441)
- [Original upstream README](UPSTREAM_README.md)

The six runtime commits remain separate from this archival handoff. Upstream:
[fspecii/LinPad](https://github.com/fspecii/LinPad), base
`01246414fe9517d883b9489ba3e9e9f3780f479e`.

GitHub Actions are disabled. Builds and verification were performed locally.
Private signing keys, developer-account credentials, pairing records, Steam
credentials and Linux user data are not included. Signed IPAs necessarily
contain their public signing certificate and provisioning profile.

Upstream and bundled components retain their original licenses; see LICENSE.md,
LICENSE.IOS and upstream source notices.
