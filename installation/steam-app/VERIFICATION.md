# LinPad Steam: what is claimed, and what is not

Everything below was produced on 17 October 2026 on this Mac and the iPad Air 13" M2
(`00008112-000924EE14D9A01E`). The project's rule applies: a result counts only when it was
observed, and this file says plainly where that has not happened yet.

## Claimed, with the artifact

| Claim | Evidence |
| --- | --- |
| The launcher behaves as designed: Big Picture first, classic UI once on a rejected `-gamepadui`, sticky fallback, `--bigpicture`/`--classic`/`--reset`, geometry validation, status file | `evidence/steam-bigpicture-test.txt` — `installation/steam-app/tests/steam-bigpicture-test.sh`, 40 checks, all pass (host only, against a fake Steam) |
| The patched `steam-local` keeps the verified behaviour when `STEAM_GEOMETRY` is unset | the same test's last group; the diff against the verified file is the variable plus its validation (git commit `dd7f06f0`) |
| The repair kit builds and carries the four new guest files | `release/build-repair-kit.sh` run by hand: `repair kit 2026101701: 1540 KB, 224 entries`, manifest lists `guest/steam-bigpicture`, `guest/steam-local`, `guest/steam.desktop`, `guest/steam.svg` |
| The app code is correct where it can be tested without a device | `xcodebuild test -scheme DesktopKit` on the iPad Pro 13" M4 simulator (`91EDD9F6-295B-4F04-B023-67A4BEFA6FFB`): **402 tests, 0 failures**, including 20 new `GameModeTests` and the whole pre-existing suite. One pre-existing test, `WallpaperPaletteTests.testFourKImageIsAnalysedWellUnder300ms`, failed in one of three runs at 0.332 s / 0.349 s against its 0.3 s budget and passed in the others; it measures image analysis, not anything this change touches |
| Both variants build from the one target | `build-local.sh desktop` → build 4478, `build-local.sh steam` → build 4480; `Info.plist` carries `LinPadVariant = desktop` / `steam` respectively |
| Both sign and verify, with the **same** app group | `evidence/signatures.txt` (`codesign -d --entitlements`), `evidence/profiles.txt` (decoded profiles): `application-identifier` `K5TW9AU245.com.konradkern.linpad[.steam]`, `com.apple.security.application-groups = [group.com.konradkern.linpad]` in both, `get-task-allow` (StikDebug's JIT) and both memory entitlements present, device provisioned, expiry 2027-10-16 |
| The second App ID exists and Xcode manages its profile | `work/signing/project.yml` target `LinPadSteam`, built with `-allowProvisioningUpdates`; profile copied to `work/signing/profiles/com.konradkern.linpad.steam.mobileprovision` |
| An IPA exists, is complete and matches its app | `LinPad-Steam-1.4.0-build4480.ipa`, 637,126,484 bytes, 1,047 members, every member's SHA-256 equal to the file on disk (`cut-ipa.py`), `sha256 ecf0060c77c743311aead44b54f65071f4503a1ca4fb3f2d0aa741d8a3538b65`, IPA name in `ipa.json`, matching dSYM (`build4480-dSYM.zip`, UUID `81786859-9FB2-35F1-BE0E-02AE1274488A`) |
| Both apps install side by side on the iPad and keep the Linux system | `install.json` for each; `devicectl device info apps` lists `Linux for iPad com.konradkern.linpad 1.4.0 4478` and `LinPad Steam com.konradkern.linpad.steam 1.4.0 4480`. The install is over the existing app group, and no rootfs archive is imported when `roots/` already exists (`app/Roots.m:294-308`) |

## Not claimed — needs the device, and one app at a time

The dedicated app was installed but **not launched**, and the desktop build was installed but
its new repair kit has **not been seen running in the guest**. A parallel session was using the
device (the ishwl compositor fix) and LinPad holds the shared Linux system while it runs, so
the sequence below is deliberately left to a moment when that session is finished.

1. **Does this Steam client accept `-gamepadui`?** The single unknown that matters most.
   On the iPad: launch **Linux for iPad** (build 4478), tap StikDebug "Enable and Run Script",
   let the repair finish, open Terminal and run `steam-bigpicture`. Expected: the Big Picture
   UI, or — if the client refuses — the classic UI plus
   `steam-bigpicture: Big Picture exited N after Ns; starting the classic UI` in
   `/tmp/steam-main.log` and `/tmp/steam-bigpicture-fallback` written.
   *Nothing in this repository has ever run `steam` with `-gamepadui`; Big Picture is not
   recorded anywhere in the project's logs.*
2. **Does the game app boot into it?** Close LinPad (it holds the lock), launch **LinPad
   Steam**, tap StikDebug for it. Expected: boot splash → "Starting Steam…" → Steam full
   screen with no desktop, the top strip revealing the bar. Not observed yet.
3. **The failure paths on the device**: the busy screen (launch the game app while LinPad runs),
   the classic-UI fallback banner, "Fill screen" geometry, and Desktop ⇄ Game Mode switching.
   All are covered by unit tests; none has been seen on the iPad.
4. **Steam → game.** Unchanged and still unqualified: the `linpad-fex-native` compatibility
   tool is installed and selected, but a game launched from Steam has never been verified. The
   game app does not change that; Big Picture's Play button is a follow-on gate.
5. **Gamepad input.** None exists in LinPad (GameController is used for keyboards only), so Big
   Picture here is touch/mouse/keyboard. Deliberately out of scope.

## How the untested parts are meant to behave if they fail

* `-gamepadui` refused → one classic-UI retry, marker file, and the game app says in its bar and
  its progress line that the classic UI started. `/etc/linpad/steam-mode` (`desktop`) pins the
  classic UI with no rebuild.
* No Steam in the system → "Steam is not installed" panel with "Open desktop" (a fresh container
  has no Steam; the Linux system is not in the IPA).
* The other app holds Linux → "Linux is in use" panel naming the app to close (the lock is
  advisory and fails open: an unopenable lock file never blocks a boot).
* Steam slow or dead → 180 s watchdog (extended once when the classic fallback is on its way),
  then a panel with "Start Steam again" / "Use the classic UI" / "Open desktop".
