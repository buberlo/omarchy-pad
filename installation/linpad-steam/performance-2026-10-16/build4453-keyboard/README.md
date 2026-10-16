# Build 4453: the keyboard reaches Hollow Knight

Hollow Knight's main menu accepted Enter on the Magic Keyboard and ignored every other key.
That is a host-side routing bug, not a broken keyboard: the game's Unity/SDL toplevel enables
`zwp_text_input_v3`, and while an app has an enabled text input the iPad sends printable keys
to its own text system, where they come back as committed text. A game reads key codes and
ignores committed text, so the key was lost before it reached Linux. Enter survived by
accident: `LinuxTextInput.insertText` turns `"\n"` back into a Return key press for a field
that is not multiline.

The user's report on build 4453 is that the keyboard works. [VERIFICATION.md](VERIFICATION.md)
states exactly what that does and does not cover.

## How it works now

- `LinuxTextInputMode` (`desktop/DesktopKit/.../Linux/LinuxTextInputMode.swift`) decides
  whether an app's hardware keys go to the app as key codes or to the iPad text system.
  - `automatic` (default): the apps on the list get key codes; `hollow_knight.x86_64` is on it.
  - `keys`: every app, for another game that turns out to need it.
  - `compose`: the previous behaviour, as a per-install rollback that needs no rebuild.
- Settings → Keyboard → "Keys in Linux apps" shows the mode and the comma-separated Wayland
  app ids. The id is the one ishwl reports with the toplevel (`map toplevel 1 'Hollow Knight'
  hollow_knight.x86_64` in `/tmp/ishwl.log`).
- A composition already in progress (dictation, emoji, a dead key like Option-E) still goes
  through the text system, and the on-screen keyboard is unaffected: only hardware keys are
  routed differently.

Everything else about the Linux input path is unchanged, including the keyboard layout
(`keymap: de mac -`), the Option-key policy and the arrow/function keys, which were already
sent as key codes.

## Starting the game on the current install

As before, in LinPad's Terminal:

```sh
sh /tmp/hollow4452-egl
```

The launcher is in the device's Linux `/tmp`; `device-scripts/` holds nothing for this build
but the keyboard probes below, and build4452's `device-scripts/hollow4452-egl` is its
archived source.

## The second half: input latency (deployed, key log verified)

The same investigation found that the compositor held its input queue for up to
`ORDER_TIMEOUT_MS` (250 ms) after every non-modifier key press while a text input was active,
because a bare key event has nothing for the app to acknowledge. That makes a released key
keep moving and every later press lag. It is fixed in `bd6e9881`, built, and **installed on the
device on 16 October 2026** — confirmed by the compositor's own key log, in which printable
keys (X, Z, Y, Space) now appear as key events for the first time. The deployment, its numbers
and what it still does not prove are in [ishwl-deployed/README.md](ishwl-deployed/README.md);
the rollback command and the exact agent commands used are archived beside it.

## Evidence in this directory

| File | What it is |
| --- | --- |
| `VERIFICATION.md` | What is claimed, what is not, and the artifact facts |
| `source-changes.patch` | `git show b432013e`, the exact change in this build |
| `source-commit.txt` | The source commit of the build |
| `build-local.sh` | The xcodebuild command that produced the app |
| `build-manifest.json` | Build identity, digests and the verification results |
| `signing.log`, `signed-app-verification.log` | Signing, and codesign's verification of the staged bundle |
| `install.log`, `install.json` | devicectl's install of the signed bundle |
| `device-scripts/` | The keyboard probes used for the deployment steps in ishwl/README.md |
| `ishwl/` | The prepared, unverified compositor fix and its build and ABI evidence |
| `source-evidence.json` | Digests of the build log and the verification logs in the workspace |
| `SHA256SUMS` | Digests of the deliverables, as paths under the workspace root |
| `REPOSITORY-SHA256SUMS` | Digests of every file in this directory |

No IPA was cut for this build: the signed bundle in `work/performance/build4453/Payload/`
was installed directly, which is what the device runs. The matching dSYM is archived beside
it as `work/performance/build4453/LinPad-1.4.0-build4453-dSYM.zip` (both in `SHA256SUMS`);
`shasum -a 256 -c SHA256SUMS` from the workspace root verifies them.

## Unit tests

```sh
cd desktop/DesktopKit
xcodebuild test -scheme DesktopKit -destination "platform=iOS Simulator,id=<an iPad simulator>"
```

379 tests, 0 failures on 16 October 2026 (the simulator used was "Release Documents iPad",
91EDD9F6-295B-4F04-B023-67A4BEFA6FFB; `desktop/UXHarness/run-all.sh`'s default device was not
installed on this Mac).
