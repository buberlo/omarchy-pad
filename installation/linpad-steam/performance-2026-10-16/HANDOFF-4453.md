# Latest handoff: keyboard fix deployed, key log verified, gameplay and Steam next

The compositor fix is on the device and — unlike build 4453 alone — it is backed by the
compositor's own log. `bd6e9881`'s `ishwl` (`a6ed7453…`, 162 856 bytes) replaced the system's
compositor (`d47335e0…`) on 16 October 2026; the old binary is kept on the device as
`/tmp/ishwl-before-kb4453`. In one play session the log shows **printable keys as key events for
the first time**: X 116×, Z 30×, Y 14×, Space 44×, alongside Enter 18× and the arrows. Before
this, those keys produced no key event at all — the host sent them as committed text, which the
game ignores. [Deployment, numbers and limits](build4453-keyboard/ishwl-deployed/README.md).

Also measured in the same session: 1 026 game-view frame deliveries in a 30 s window during play
(34.2/s; 6 839 rows over the whole 140.7 s session), a live unmuted uncorked Pulse stream for the
game's pid, app footprint 2 986 MB of the 8 192 MB allowance with `kills 0`, and the game on
`Virtio-GPU Venus (Apple M2 GPU)`, Mesa 26.2.4-1, swapchain 1366×950.

## What is still open

1. **Human confirmation of gameplay and audio** with this compositor: does the knight move,
   jump, attack, respond to the menus, and is it audible? The key codes prove input arrived;
   they do not prove the game reacted.
2. **Text-field ordering**: the change deliberately lets only Return, Tab and Backspace wait
   for the app. No human has typed into a text field on the device since the restart, but the
   rule is now **measured** headless and before/after with
   `wl-bridge/tools/textinput-order-test.sh`: text after a bare key press arrives in 0.0 ms
   with this change against 260.6 ms with the previous revision, while text after Backspace
   still waits 258 ms. Container measurement of the same code, not the device.
3. **The 250 ms figure** is arithmetic from `ORDER_TIMEOUT_MS`; the same headless test
   measures 258 ms (in the container, not on the device).
4. **Steam**: the game was started with `sh /tmp/hollow4452-egl` by the command agent, exactly
   as by hand. The wrapper (`linpad-hk-fex`) and the launch-options editor
   (`kb4453-steam-launchopts`, 12 local tests green) are prepared but not installed, and the
   Steam route is not qualified. Note that a Steam toplevel appeared in the log before this
   session and a launch attempt from it failed with `hollow_knight.x86_64: not found`, so the
   Steam entry currently starts nothing.
5. **A numerical FPS result** in the usual sense. Frame *deliveries* are not frames per second
   and are not presented as such.

## The device is drivable from the Mac

The user pasted one bootstrap line; it installed the binary, the frame-log hook and a bounded
command agent (`/usr/local/bin/kb-agent`, 180 minutes, no autostart, removable with
`kill "$(cat /tmp/kb-agent.pid)"; rm -f /usr/local/bin/kb-agent`). The Mac queues one shell
command at a time through `work/performance/agent-run` and the answers land in
`work/performance/reports/`. The stop script of a desktop session restart kills only ishwl,
dbus-daemon and pulseaudio, so the agent survives it. Rollback of the compositor is
`agent-commands/4-99-rollback-ishwl.sh` plus a session restart.

The device lease in the Madeira workspace still names `linpad` as owner with
`linpad-install-active` and `work/linpad-lease-token` matches it. Device Hub keyboard capture
stays off; JIT needs StikDebug's approval after every app restart.
