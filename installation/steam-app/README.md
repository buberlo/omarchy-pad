# LinPad Steam: the dedicated game app

A second app, built from the same source and target as LinPad, that boots the Linux system
and goes **straight into Steam's Big Picture UI, full screen, with no desktop** — the
Bazzite/SteamOS-*style* session this project can actually have (the guest has no DRM/KMS
master, so gamescope and a real SteamOS are out; ishwl is the compositor).

* Home screen: **LinPad Steam** (`LinPad Steam.app`, bundle id `com.konradkern.linpad.steam`)
* Same Linux system as LinPad: the app group `group.com.konradkern.linpad` holds `roots/`,
  so Steam, the ARM64 Steam runtime, FEX, Mesa/Venus, Hollow Knight and the saves are shared,
  **not installed twice**
* One app at a time: both apps mount the same fakefs, so `app/AppGroup.m` takes a lock before
  the kernel mounts it. The other app then shows "Linux is in use" and says which app to close
* Not a SteamOS install, and not a gamepad session: Big Picture is driven by touch, mouse and
  keyboard (there is no game-controller path in LinPad yet)

## What happens on launch

1. The app boots the same Linux system the desktop app uses (JIT through StikDebug, as always).
2. `receive/guest/steam-bigpicture` (installed by the repair kit, see below) starts the
   **verified** Steam route: `/usr/local/bin/steam` → `steam-local` → private Xvnc + Steam
   (`steamrtarm64`) + a VNC viewer shown as one `ishwl` window named `Steam`.
   It adds `-gamepadui` for Big Picture; if this Steam build refuses it (non-zero exit inside
   a minute) it records `/tmp/steam-bigpicture-fallback` and starts the classic UI once.
3. `GameModeCoordinator` claims that toplevel (app id `Steam`) and `GameModeRootView` shows it
   full screen — no wallpaper, panel, dock or window chrome. A 24 pt strip at the top edge
   reveals the bar: Restart Steam · Classic UI · Fill screen · Restart Linux · Desktop.
4. "Desktop" switches the app to the ordinary LinPad desktop (needed for Steam sign-in,
   installing games, Terminal work). Settings › Desktop › "Boot into Game Mode" switches back
   at the next start.

The pieces: `desktop/DesktopKit/Sources/DesktopKit/Core/Game/` (`LinPadVariant`,
`GameModeConfiguration`, `GameModeCoordinator`, `GameModeRootView`), the claim path in
`Linux/DesktopController+Linux.swift`, the root-view choice in `app/Desktop/DesktopBridge.swift`,
and the session lock in `app/AppGroup.{h,m}`.

## Build, sign, package, install

Run everything from the **workspace root** — the directory that holds `work/`, i.e. the parent
of the `LinPad` checkout:

```sh
# 1. Build and stage one variant (both are the one iSH-ARM64 target; LINPAD_VARIANT differs)
sh work/LinPad/installation/steam-app/build-local.sh steam     # -> work/performance/steam-app-steam/Payload/LinPad Steam.app
sh work/LinPad/installation/steam-app/build-local.sh desktop   # -> work/performance/steam-app-desktop/Payload/iSH ARM64.app

# 2. Sign it (own bundle id, same app group; asserts the profile, then reads the signature back)
python3 work/LinPad/installation/steam-app/sign-variant.py --variant steam \
    --app "work/performance/steam-app-steam/Payload/LinPad Steam.app"

# 3. Cut the .ipa and check every member against the files on disk
python3 work/LinPad/installation/steam-app/cut-ipa.py --variant steam

# 4. Install (keeps the app group and therefore the Linux system)
xcrun devicectl device install app --device 00008112-000924EE14D9A01E \
    "work/performance/steam-app-steam/Payload/LinPad Steam.app"
```

`build-local.sh` takes the root filesystem from an already signed payload
(`ROOTFS_ARCHIVE=work/signed/Payload/iSH ARM64.app/root.tar.gz`), which is how every release
here works: the archive is only used by a *fresh* container, and an install over an existing
app keeps Linux, Steam, games and saves. Both variants embed the same ~622 MB archive, so an
IPA is ~637 MB even though the game app never needs it.

## The guest side: the repair kit

Anything the guest must have is shipped inside the app (`release/build-repair-kit.sh`) and
installed by `release/guest/linpad-repair` when `release/guest/repair-kit-version` is newer
than the system's. This change adds:

| Kit file | Installed as | Note |
| --- | --- | --- |
| `guest/steam-bigpicture` | `/usr/local/bin/steam-bigpicture` | Big Picture launcher, `--geometry`, `--classic`, one fallback |
| `guest/steam-local` | `/usr/local/bin/steam-local` | the verified launcher, now honouring `STEAM_GEOMETRY` (default 1024x700, so unchanged) — replaced only when it is the known version, with `steam-local.linpad-original` beside it |
| `guest/steam.desktop` | `/usr/share/applications/steam.desktop` | the desktop launcher entry; the old one lived in `~/.local/share/applications`, which the desktop never lists |
| `guest/steam.svg` | `/usr/share/icons/hicolor/scalable/apps/steam.svg` | icon for the guest's icon cache |

`installation/steam-app/tests/steam-bigpicture-test.sh` runs the launcher against a fake Steam
on the host: 40 checks over argv, the status file, the fallback, `--geometry`, `/etc/linpad/steam-mode`
and the `steam-local` patch. `installation/linpad-steam/scripts/guest/steam-local` is kept in
step with the kit's copy.

## Provisioning: the second App ID

The game app needs its own profile, and that profile must allow the **same** app group as
LinPad. `work/signing/project.yml` has a `LinPadSteam` target for exactly this:

```sh
cd work/signing
/opt/homebrew/bin/xcodegen generate
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  /Applications/Xcode.app/Contents/Developer/usr/bin/xcodebuild \
  -project LinPadSigning.xcodeproj -target LinPadSteam -configuration Debug -sdk iphoneos \
  -destination 'generic/platform=iOS' -allowProvisioningUpdates -allowProvisioningDeviceRegistration build
cp build/Debug-iphoneos/LinPadSteam.app/embedded.mobileprovision \
   profiles/com.konradkern.linpad.steam.mobileprovision
```

Xcode registers the App ID, enables App Groups for it and issues a managed profile; the first
run needs the Apple ID session Xcode already has on this Mac. `sign-variant.py` reads the
stable copies in `work/signing/profiles/` and refuses to sign unless the profile carries the
application identifier, `get-task-allow` (StikDebug's JIT), both memory entitlements and
`group.com.konradkern.linpad`. Decoded copies are in `evidence/profiles.txt`.

If the portal ever refuses the shared group, the fallback is the desktop bundle id with
`LINPAD_VARIANT=steam` (same app, no second app): the game mode is a build setting and a
persisted preference, not a second code base.

## Status, and what is not done yet

Proven here: both variants build; both sign and verify with the shared app group
(`evidence/signatures.txt`); the repair kit builds with the four files above; the launcher's
40 host-side checks pass; the DesktopKit suite is 402 tests / 0 failures.

**Not proven yet — needs the device, and only one app may hold Linux at a time:**

1. Tap through StikDebug's "Enable and Run Script" for **Linux for iPad** (build 4478 is
   installed) so the new repair kit lands in the guest, then in its Terminal run
   `steam-bigpicture` and watch: Big Picture, or the classic fallback with a note in
   `/tmp/steam-main.log`. This is the one unknown that matters most: whether this ARM64
   Steam client accepts `-gamepadui`.
2. Close LinPad (swipe it away — it holds the Linux lock while it runs), then launch
   **LinPad Steam**, tap StikDebug for it, and watch it boot straight into Steam full screen.
3. Try the bar's Desktop switch, then Settings › "Boot into Game Mode" to come back.

Still open, and out of scope here: Steam → game launching (the `linpad-fex-native` compat tool
is installed and selected, but a Steam-launched game is unqualified), game-controller input,
and Steam UI performance at anything above the verified 1024x700 ("Fill screen" is opt-in for
that reason).
