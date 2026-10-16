# LinPad iPad: Steam and FEX

Private archive of the LinPad work verified on 16 October 2026.

**Current device checkpoint:** development-signed LinPad 1.4.0 build 4451 runs on the M2 iPad. The installed Linux Hollow Knight reaches its German main menu through FEX and Venus Vulkan on Apple M2. A foreground 30-second menu capture measured 212 compositor deliveries (7.07/s at 659×540). The user reported audible but severely stuttering music on the preceding build. Normal gameplay and smooth audio remain unqualified.

Build 4451 identifies the dominant slow fallback instructions as LDAPUR/STLUR. A separate FEX feature-override experiment exits before Unity initialization and is not a working configuration. [Current evidence and limitations](installation/linpad-steam/performance-2026-10-16/build4451/VERIFICATION.md).

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
