# Build 4452 main-menu follow-up and user handoff

On 16 October 2026 the operator launched the installed game from LinPad's terminal using `sh /tmp/hollow4452-egl`. The German Hollow Knight main menu was visibly rendered on the physical iPad; the saved screenshot is the actual observation, not a mockup. The game remained open. The user then reported: "es läuft flüssig". This establishes user-reported smoothness for the observed run. No new numerical FPS capture, matched foreground performance measurement, smooth-audio acceptance or normal gameplay verification was completed.

The connected iPad/Magic keyboard has a remaining input problem. The user's final clarification is: Enter works, all other keys do not. Earlier descriptions mentioned only the arrow keys; the final clarification supersedes that narrower description. A pointer-focus attempt and a remote Down key did not visibly move the main-menu selection. No cause or keyboard fix is established. No input-source code or settings were changed. DeviceHub's Mac keyboard capture remained off.

Recording was requested, then the user took over recording and further play. Two screencapture invocations exited 1 without producing a file. The ffmpeg AVFoundation attempt remained in input configuration, produced no MP4 and was terminated on the user's handoff request. The included logs preserve the attempted methods; there is no video recording to claim or deliver. No camera or microphone input was selected.

The user requested saving the current work, updating the docs, committing/pushing and stopping for now. No further device/game/input changes are authorized by that handoff alone. Resume only on a new user request. The installed build, Linux data and current open game are left available to the user. Existing slot 1 is the user's save; slot 2 was created for prior tests.

## Starting the installed game yourself

1. Open LinPad (the iPad app named Linux for iPad).
2. If StikDebug asks for JIT, tap Enable and Run Script and return to LinPad.
3. Open Terminal in the Linux desktop and run:

```sh
sh /tmp/hollow4452-egl
```

Keep that terminal open while playing. This is the exact saved launcher used successfully on this device. Do not start a second copy while the game is already open. The temporary-path script is present in the current installed Linux filesystem; it is not automatically restored by a fresh root filesystem. Its archived source is ../build4452/device-scripts/hollow4452-egl. The desktop game's icon and the final production Steam-to-game route have not been qualified as equivalent launch paths.
