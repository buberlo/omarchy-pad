# LinPad iPad: Steam and FEX

Private archive of the LinPad work verified on 16 October 2026.

**Current device checkpoint:** development-signed LinPad1.4.0 build4453 is installed and running on the M2 iPad. It adds the keyboard routing fix: an app on `LinuxTextInputMode`'s list receives its hardware keys as key codes instead of as iPadOS text input, which is why Hollow Knight previously took Enter and nothing else. [Build4453 verification](installation/linpad-steam/performance-2026-10-16/build4453-keyboard/VERIFICATION.md).

**The user reports that the keyboard in Hollow Knight works on build 4453** — the first non-Enter key reported working in the game. A per-key result, a screenshot, gameplay/audio acceptance and a numerical performance result are still open, and the second, prepared fix (the compositor holding the input queue for up to 250 ms per key, unverified and not yet installed) is described in [build4453-keyboard/ishwl](installation/linpad-steam/performance-2026-10-16/build4453-keyboard/ishwl/README.md). [Current handoff and next steps](installation/linpad-steam/performance-2026-10-16/HANDOFF-4453.md).

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
