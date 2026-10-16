# LinPad iPad: Steam and FEX

Private archive of the LinPad work verified on 16 October 2026.

**Verified:** development-signed LinPad 1.5.0 build 4441 installed on the M2 iPad;
Steam ARM64 reaches its sign-in screen; FEX static runtime probes pass;
the rebuilt app renders a Vulkan cube. Sign-in and games were not tested.

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
