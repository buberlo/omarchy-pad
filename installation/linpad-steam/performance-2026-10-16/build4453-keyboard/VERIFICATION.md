# Build 4453 verification: the keyboard in Hollow Knight

Build 4453 is LinPad 1.4.0, source commit `b432013e` on top of the archived `dd179fdc`,
signed, installed and booted on the physical M2 iPad. It carries one behaviour change:
a Linux app listed in `LinuxTextInputMode` receives its hardware keys as key codes instead
of as iPadOS text input.

## What is claimed

- The app builds, signs and passes its own verification: `codesign --verify --deep --strict`
  reports "valid on disk" and "satisfies its Designated Requirement"; the profile runs to
  2027-10-16T06:33:44Z; the app and its dSYM share UUID `8A5CEAD6-08E8-3BB6-8CF6-835E6C33B98E`.
  See signed-app-verification.log.
- devicectl installed the signed bundle over build 4452 (install.log, install.json).
- DesktopKit's unit tests pass on the iOS simulator: **379 tests, 0 failures**, including the
  8 new `LinuxTextInputModeTests`. No test was skipped or disabled.
- **The user reports that the keyboard in Hollow Knight works on this build.** After the
  install they started the game with `sh /tmp/hollow4452-egl` in LinPad's Terminal and
  answered the request to test the arrow keys, W/A/S/D, Enter and Esc with "that worked".

That is the whole claim. It is a user report about one run of one game, and it is the first
time any non-Enter key has been reported to work in Hollow Knight.

## What is not claimed

- No per-key list was captured: which of the arrow keys, W/A/S/D, Space, Enter and Esc were
  tried, and what each did, is not recorded. "That worked" is all there is.
- No screenshot, no recording, no frame, no audio and no frame-rate measurement.
- No log-level proof that the host sent a key code. The one-line-per-key log that would show
  it is in the unverified `wl-bridge` change (see ishwl/README.md) and is not installed on
  the device. With build 4453's app and the device's current compositor, a printable key
  still leaves no trace in `/tmp/ishwl.log`.
- No gameplay acceptance: movement, combat, menus beyond the tested keys, and the transition
  from Steam into the game are all still open, exactly as HANDOFF-4452.md recorded them.
- The 250 ms input-latency defect that the same investigation found is fixed in source
  (`bd6e9881`) but **not deployed and not verified on the device**. Build 4453 was tested
  with the device's original compositor, so the keys the user tried arrived without it.
- Steam-to-game production launch is untouched. The game was started from LinPad's Terminal.

## Where the fix is

| File | Change |
| --- | --- |
| `desktop/DesktopKit/Sources/DesktopKit/Linux/LinuxTextInputMode.swift` | New: the mode, the app-id list, `sendsKeysDirectly(appID:)`, the Settings row |
| `desktop/DesktopKit/Sources/DesktopKit/Linux/LinuxTextInput.swift` | `routesToTextSystem` returns false for a listed app while no composition is marked |
| `desktop/DesktopKit/Sources/DesktopKit/Apps/SettingsApp.swift` | One row in Settings → Keyboard |
| `desktop/DesktopKit/Tests/DesktopKitTests/LinuxTextInputModeTests.swift` | New: 8 tests over the mode, the list and the id matching |

`source-changes.patch` is `git show b432013e`, the exact change in this build.

## Rebuilding and reinstalling

`build-local.sh` is the xcodebuild command that produced this app, including the Zig cache
and compiler wrapper paths that build 4452's copy of the script omitted. Signing and install
follow signing.log and install.log. Nothing else in the tree was modified for this build;
in particular the rootfs is the installed one, so the Linux system, Steam, the game and both
save slots survive the install.
