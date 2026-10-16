# ishwl stops holding the input queue for 250 ms

Status: **built, checked, and deployed on 16 October 2026** (see
[../ishwl-deployed/README.md](../ishwl-deployed/README.md) for the deployment, the key log that
confirms the new binary is running and what is still unverified). This document stays as the
build and ABI record: how the binary was produced, what was checked before it was installed,
and the deployment steps as they were planned.

## The defect

`wl-bridge/src/textinput.c` orders keys and committed text: while the focused app has an
enabled text input, an update waits for the app's next `commit` before the queue moves on.
The condition also covered *bare key presses*:

```c
waiting_for_app = ti && (!in->is_key || (in->pressed && !is_modifier(in->keycode)));
```

A key press produces no request the app could acknowledge -- `deliver()` sends
`wl_keyboard.key` and nothing else -- so no `commit` follows it. The queue therefore waited
for the full `ORDER_TIMEOUT_MS` (250 ms) after every non-modifier key press:

- the release of a held key is delivered with that delay, so movement keeps running after the
  key is let go;
- every key press after the first is delivered with that delay, because the queue only
  advances one press per timeout.

Enter still worked in Hollow Knight (a menu submit reads the press), which is why the defect
stayed invisible behind the routing bug that build 4453 fixed.

## The change (`bd6e9881`)

Only Return, Tab and Backspace can edit text. For a field that is not multiline the host sends
Return and Tab as key presses and deletes with Backspace, so only their press keeps waiting --
exactly the case the wait was written for (committed text must not overtake the edit). Every
other key press releases the queue as before. `is_modifier()` is gone: no modifier is an
edit key, so the condition implies it.

The same commit logs one line per key at `-v`, like the existing `keymap:` line:

```
ishwl 12345.678: key 17 down
```

The app starts the session with `setsid ishwl-session --runtime-dir <dir> -v >/tmp/ishwl.log 2>&1`
(`LinuxGUIBridge.connect()`), so this needs no options change. It is what makes the next
device test *prove* that a printable key arrived as a key code: with the host-side text input
a printable key produces `ime`/`text` messages and no `key` line at all.

## Building it (no rootfs rebuild)

`build-ishwl-aarch64.sh` builds it on this Mac in the aarch64 `linpad-fex` colima VM from the
same Alpine 3.21 packages `release/build-rootfs.sh` installs in the guest, and copies the
result into this directory. The guest builds its own copy the same way at rootfs build time
(`make CC=tools/ccwrap && make install`); `ccwrap` only works around iSH's `wait4` returning
EINTR while compiling, so a native build needs no wrapper.

`abi-verification.txt` records the check: the shipped compositor's `.text` and stripped size
are identical to a build of the archived revision `a6e0672e`, the patched binary is +96 bytes
of `.text`, and its undefined symbols are the same set as the shipped one. `artifact-sha256.txt`
has the digests.

## Deploying it

1. On the Mac, serve the workspace transfer directory and the artifacts:

   ```sh
   cd work/performance && python3 server.py        # 192.168.0.104:8766
   ```

2. In LinPad's Terminal, on the device:

   ```sh
   wget -qO /tmp/kb4453-probe http://192.168.0.104:8766/kb4453-probe && sh /tmp/kb4453-probe
   ```

   That records the guest state (package versions, the current `/usr/local/bin/ishwl` and its
   digest, `/etc/ishwl/options`, the tail of `/tmp/ishwl.log`) and posts it back to
   `work/performance/reports/probe.txt`. Keep it as the before-picture.

3. Install the patched compositor:

   ```sh
   wget -qO /tmp/kb4453-install-ishwl http://192.168.0.104:8766/kb4453-install-ishwl && sh /tmp/kb4453-install-ishwl
   ```

   It verifies the SHA-256, keeps the old binary in `/tmp/ishwl-before-kb4453`, and installs
   with `mv` (overwriting a running executable fails with "text file busy").

4. Restart the session: Quick Settings → **Restart Desktop Session**. The running session
   keeps its own inode until then, so the old compositor is still live before this step.

5. Start the game (`sh /tmp/hollow4452-egl`), press keys, then collect the evidence:

   ```sh
   wget -qO /tmp/kb4453-keylog http://192.168.0.104:8766/kb4453-keylog && sh /tmp/kb4453-keylog
   ```

   Report in `work/performance/reports/probe.txt`: the `key <code> down/up` lines for the keys
   that were pressed, the distinct key codes, and the text-input and keymap lines.

6. Accept or reject on the device: the keys must still respond, and a held movement key must
   stop the character when it is released rather than keep it moving. Check a text field as
   well (Terminal or Firefox): typing, Backspace and Enter must stay in order.

   Rollback: `cp /tmp/ishwl-before-kb4453 /usr/local/bin/ishwl.new && mv /usr/local/bin/ishwl.new /usr/local/bin/ishwl`
   and restart the session. Nothing else on the device changed, and this repository's rootfs
   still carries the original binary.

## What is not done

- The patched binary has never run. The abi check compares symbols; it does not load it.
- No text field has been tested with the new ordering rule, so the "typed text is not
  overtaken by an edit key" guarantee is argued from the code, not observed.
- The 250 ms figure is arithmetic from `ORDER_TIMEOUT_MS`, not a measurement.
