# Exact Unity SDL initialization failure

Read-only binary/source audit and local diagnostic execution, 2026-10-16. No game binary modification, device operation or gameplay result.

Input is the privately obtained `UnityPlayer.so`, 39,255,232 bytes, SHA256 `12ce7af93c41b13b3e7d9b75b591336cc56c9ce113714143fd897cc1dcf00e33`. Keep that proprietary binary local; the diagnostic source is independently written.

## Confirmed caller and rollback

`PlayerMain` calls embedded SDL initialization at `UnityPlayer+0x159ab0d`, passing `0x3221` (timer, video, joystick, haptic, game controller). It does not check the returned integer. It immediately reads SDL's current video pointer at `+0x2666f28` and prints the selected backend. If the pointer is null, that getter first overwrites SDL's original error with `Video subsystem has not been initialized`. Therefore the ordinary Unity log loses the actual initialization error.

The embedded initialization function is `+0x2436114`. It initializes video before joystick and haptic, tracking successful flags. A later failure reaches `+0x24363d7`, calls the embedded quit-subsystems function `+0x2436d73` with those flags, and returns `-1`. The physical trace's text-input initialization occurs after the video display-count check; the subsequent global/surface teardown is consistent with this rollback. Null-backend text alone is not sufficient evidence, because other Unity logs may recover afterward.

This control flow agrees with the upstream [SDL initialization source](https://raw.githubusercontent.com/libsdl-org/SDL/release-2.32.4/src/SDL.c). The exact embedded offsets, rather than an assumed SDL release, establish the behavior in this game.

## Confirmed kernel/API mismatch

Embedded SDL UDEV initialization at `+0x246941f` obtains a udev object and calls `udev_monitor_new_from_netlink(udev, "udev")`. A null monitor is fatal. Both joystick and haptic initialization can propagate this failure. The filter-add and monitor-enable return values are ignored in this embedded SDL code; pretending unsupported BPF filtering succeeded is unnecessary for the observed fatal path. Upstream [SDL UDEV initialization](https://raw.githubusercontent.com/libsdl-org/SDL/release-2.32.4/src/core/linux/SDL_udev.c) shows the same monitor requirement.

Current LinPad `fs/netlink.c:436` accepts protocol 0 (`NETLINK_ROUTE`) and rejects protocol 15 (`NETLINK_KOBJECT_UEVENT`) with `EAFNOSUPPORT`. The real installed libudev attempts `socket(16, 526339, 15)`: AF_NETLINK, SOCK_RAW with CLOEXEC and NONBLOCK, protocol 15.

## Local reproduction

`unity-sdl-init-probe.c` loads the real Unity library without calling PlayerMain. It verifies 16 instruction bytes at each private function offset before calling the embedded init/error/quit functions. It reports the original error before any video getter can overwrite it. It never patches code or creates a game renderer. A separate `udev` mode calls the real public libudev API directly.

Local macOS-hosted LinPad ARM64 CLI with the original FEX binary produced:

| Test | Result |
| --- | --- |
| Direct libudev monitor | NULL, errno 97, Address family not supported by protocol; process exit 5 |
| Exact Unity SDL haptic `0x1000` | -1, `Could not initialize UDEV`; probe exit 10 |
| Exact Unity SDL joystick `0x200` | -1, `Could not initialize UDEV`; probe exit 10 |
| Exact Unity SDL timer + video `0x21` | -1, `No available video device`; expected local negative control because there is no compositor |

Raw logs, concise excerpts and hashes are recorded as `local-unity-sdl-*.txt` and `local-unity-sdl-results.json`.

The parent executed the same probe on physical build 4445 from 11:09:27 to 11:09:41 UTC. `unity-sdl4445-before-fail.txt` confirms direct UDEV errno97, isolated haptic/joystick failures, successful timer+video with a non-null video pointer, and full `0x3221` failure with a null video pointer. This directly demonstrates video rollback on the device. The successful video-only call retains an SDL error from an earlier internal check; its return value is 0, so that stale error string does not turn the result into failure.

The parent coordinates the kernel protocol-15 fix. Local post-fix tests confirm that real libudev monitor creation succeeds and both exact Unity haptic/joystick calls return 0. The subsequent libudev filter-enable step still returns `-EINVAL` because BPF filtering remains unsupported; the embedded SDL ignores that result. This is deliberately an idle receive endpoint, not a full hotplug implementation. The native endpoint regression passes 35 checks after the final mask-preservation change, including idle poll, unique port binding, full group-mask readback, last-close reuse and existing route replies. See `local-netlink-after-results.json` and `local-netlink-final-provenance.json` for exact scope and source hashes.

Another agent's independent fatal-RIP audit identifies Unity's actual later crash as an element-zero access in a zero-sized display list, supporting this causal chain. Physical post-fix subsystem setup and game recovery still require fresh runs.

## Device diagnostic prepared, not executed here

`serve/unity-sdl-init-probe` SHA256 `278f4dbbfc1e2904c2215ca046adce97571cbe8ac8fad665c2d0acd2911f4b81`.

`serve/unity-sdl4445-probe` SHA256 `f8c5becd7ba38e40b71c119eaf11912c0e2838eb1fa59ca94063847ab31a73b9` runs direct UDEV and isolated haptic, joystick, timer+video and all requested subsystems. Each has a 45-second internal alarm and a 50-second external deadline plus 3-second kill grace. It uses the patched FEX runtime and existing Wayland socket. No renderer or game entry point is created.

The original probe remains unchanged. A separate `unity-sdl-init-probe-v2` adds the display count from the verified SDL video-device field at offset `0x2d8`, only when the pointer is non-null. Its SHA256 is `42ea48be641a1211f5291b224c7cc34585b6a04a8323b2855c33c03d28bd30d6`; `unity-sdl-after-probe` wrapper SHA256 is `a314781e1e4ca08f696703546e43f468b111961007883fa145d2613bb526b06b`. The original and v2 diagnostics remain bound to the exact Unity build above. Passing these probes proves subsystem setup, not window rendering or playable performance.
