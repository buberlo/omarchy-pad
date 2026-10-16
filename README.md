# LinPad iPad: Steam and FEX

Private archive of the LinPad work verified on 16 October 2026.

**Current device checkpoint:** development-signed LinPad1.4.0 build4452 is installed and running on the M2 iPad. It adds native translation for the LDAPUR/STLUR memory instructions identified as the dominant slow fallback. The physical ARM64 regression passes19151 checks, including8190 memory cases,200 code rewrites and10000 thread exchanges; both prior SMC probes also pass. [Build4452 verification](installation/linpad-steam/performance-2026-10-16/build4452/VERIFICATION.md). [Private signed build and symbols](https://github.com/buberlo/linpad-ipad-steam/releases/tag/ipad-steam-build-4452).

Hollow Knight now visibly reaches the German main menu on build 4452; the user reports that it runs smoothly. A new numerical performance measurement and normal gameplay/audio acceptance remain open. The connected Magic Keyboard currently sends Enter successfully, while the other keys reportedly fail; no keyboard fix is claimed. The user is taking over play and recording. [Current handoff and start instructions](installation/linpad-steam/performance-2026-10-16/build4452-menu-followup/README.md).

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
