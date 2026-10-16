# LinPad iPad: Steam and FEX

Private archive of the LinPad work verified on 16 October 2026.

**Current device checkpoint:** development-signed LinPad1.4.0 build4452 is installed and running on the M2 iPad. It adds native translation for the LDAPUR/STLUR memory instructions identified as the dominant slow fallback. The physical ARM64 regression passes19151 checks, including8190 memory cases,200 code rewrites and10000 thread exchanges; both prior SMC probes also pass. [Build4452 verification](installation/linpad-steam/performance-2026-10-16/build4452/VERIFICATION.md).

Hollow Knight performance on4452 is still unmeasured. The previous4451 baseline reached the German main menu at7.07 compositor deliveries/s at659×540. Normal gameplay, smooth audio and the final Steam-to-game launch remain unqualified. The failed feature-override experiment and all earlier evidence remain archived.

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
