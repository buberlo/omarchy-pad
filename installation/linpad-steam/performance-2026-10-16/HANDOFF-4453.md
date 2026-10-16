# Latest handoff: the keyboard works, the latency fix is pending

The user reports that the keyboard in Hollow Knight works on build 4453, installed on the
physical iPad. It is the first non-Enter key ever reported to work in the game, and it was
reported after the fix in `b432013e` (a listed app's hardware keys go to it as key codes
instead of into the iPadOS text system). The exact scope of that report, and everything it
does not cover, is in [build4453-keyboard/VERIFICATION.md](build4453-keyboard/VERIFICATION.md).
No per-key list, screenshot, recording, frame rate or audio was captured.

Next, in order:

1. **Deploy the prepared compositor fix** (`bd6e9881`): the device still runs the compositor
   from its Linux system, which holds the input queue for up to 250 ms after every
   non-modifier key press while a text input is active. Built, ABI-checked, not installed.
   [build4453-keyboard/ishwl/README.md](build4453-keyboard/ishwl/README.md) has the change,
   the artifacts and the exact steps (probe, install, Restart Desktop Session, key log).
   The same binary's key log is what turns "the user says it works" into evidence: a
   printable key must appear as `key <evdev> down` in `/tmp/ishwl.log`.
2. **Then the gameplay gate that is still open**: movement, combat, menus, audio, and a real
   numerical performance result. Nothing about gameplay is proven by a working keyboard.
3. **Then Steam**: integrate the proven launch route into Steam and verify Steam-to-game
   launch. The game is still started with `sh /tmp/hollow4452-egl` in LinPad's Terminal; the
   desktop shortcut and the Steam route are not qualified.

Build 4453 is LinPad 1.4.0, commit `b432013e` on the archived `dd179fdc`, signed, installed
over 4452 in place (same bundle id, no Linux system change: the bundled rootfs is the
installed one, `root.version 202610031701`, so Steam, the game and both save slots are
untouched). Slot 1 is the user's save; slot 2 was created for earlier tests.

The device lease in the Madeira workspace still names `linpad` as the owner with
`linpad-install-active`, and `work/linpad-lease-token` matches it, so device work is ours
without acquiring anything. Release it when device work ends. Device Hub keyboard capture
stays off. JIT needs StikDebug's "Enable and Run Script" after every app restart.

The earlier build checkpoints stay as recorded in HANDOFF-4452.md; nothing in them is
superseded by this build except the input-routing statement they carried ("Enter works, no
other key does"), which is now fixed on the device.
