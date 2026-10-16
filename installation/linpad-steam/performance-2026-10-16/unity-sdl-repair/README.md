# Unity SDL startup and the zero-display fault

The traced Hollow Knight run on physical LinPad build 4445 failed because Unity wrote the first entry of an empty per-display vector. The original guest fault is precisely mapped to the installed Unity ELF. Independent external SDL testing on the same iPad fails Unity's combined subsystem flags with `Could not initialize UDEV`; its video-only step succeeds. The exact embedded SDL and libudev diagnostics are kept separately in `embedded-probe`.

The repair in build 4446 now passes the same embedded SDL probe on the physical iPad: all tested flag sets return0, and the combined game flags see one display. The native uevent regression also passes all 35 checks. These results qualify subsystem initialization; game rendering and playable performance remain pending. The proprietary 39 MB UnityPlayer library is not included or modified.

## Decisive crash evidence

The original fault was SIGSEGV/code1 at accessed address0, guest RIP `0xcde59a4a`. The executable mapping starts at `0xcce32000` with file offset `0x6fa000`, yielding `UnityPlayer+0x1721a4a`. The instruction is `mov qword ptr [rax],0`, with captured RAX=0 and RBX=0.

Immediately before it, Unity reads the native-display count from RVA `0x2640200`. With a count of zero, the per-display vector remains unallocated, but Unity still writes element0. The only count-setting paths found enumerate embedded SDL video state: success stores its display count; failure prints `Error getting num native displays: %s` and stores zero. The game's earlier log contains that exact error and a null selected video backend. The bounded disassembly and string addresses in `crash-path` establish this connection without assigning guessed function names to stripped code.

The faulting code is inside the object constructor entered at `0x171c810`; its upstream call at `0x147c05a` matches the first recorded return frame `0x147c05f`. Vulkan's PSO-cache message was only the preceding log milestone. Its own handler correctly substitutes zero initial cache data and invokes `vkCreatePipelineCache`; the actual fault instruction is elsewhere.

Unity subsequently emitted a second SIGSEGV/code0 while re-raising the fatal signal through libc. Its reporter field `address=0x1ad6` equals PID6870 and belongs to the signal sender union for code0; it is not another accessed fault address. The first hardware-style nil access is the fault to diagnose. This traced process naturally exited **139**. A later guarded stop found PID6870 absent and did not send a kill. It must not be confused with the previous experiment's manual stop137. Source report hashes and selected original line numbers preserve both facts.

The installed UnityPlayer SHA256 is `12ce7af93c41b13b3e7d9b75b591336cc56c9ce113714143fd897cc1dcf00e33`, GNU build ID `687ec82915ce757a0bf9ebcd6068f093707479e4`. `UnityPlayer_s.debug` is its debuglink name; no matching symbols were available. Most objdump `PlayerMain+largeOffset` labels are merely nearest-export annotations.

## External SDL control

`external-probe/sdl-subsystem-probe` dynamically loads the existing SDL2 2.32.4. It tests `SDL_InitSubSystem(0x3221)` and captures the immediate return/error, active flags and video backend. A separate process initializes timer, video, joystick, haptic and controller in order, stopping at the first failure. Each named subsystem can also be invoked alone. No window, context or Vulkan renderer is created.

Physical build4445 results: combined flags fail with return-1, no active subsystem and null video backend. Sequential timer and video succeed; video retains Wayland. This external SDL's joystick returns success despite a retained udev error string. Haptic returns-1 with `Could not initialize UDEV`. Success/error-string handling matters: the embedded SDL's behavior is checked independently rather than inferred from this external release.

The exact binary, source, build command, invocation wrapper and complete narrow device report are in `external-probe`. The shell wrapper defaults to `all sequential`; optional arguments select isolated modes. Each process has a 60-second alarm and a 70-second outer timeout. No diagnostic automatically changes the installed runtime.

## Verification boundary

The display-state fault and the external combined-subsystem failure are physically observed. The exact embedded SDL probe also reproduces the failure on build 4445: isolated haptic and joystick initialization fail with `Could not initialize UDEV`; timer plus video succeeds; combined flags0x3221 fail and leave a null video pointer. Direct libudev monitor creation returns NULL with errno97. Local syscall tracing identifies `socket(AF_NETLINK, SOCK_RAW|SOCK_NONBLOCK|SOCK_CLOEXEC, 15)` as the unsupported request.

The focused host repair passed 35 checks in the local macOS-hosted LinPad CLI after its final group-mask correction. Exact embedded haptic and joystick initialization also returned0 in the local CLI before that mask-only correction, and libudev monitor creation succeeded.

The matched physical v2 results use the identical diagnostic SHA256 `42ea48be641a1211f5291b224c7cc34585b6a04a8323b2855c33c03d28bd30d6` and unchanged UnityPlayer library:

| Check | Build 4445 before | Build 4446 after |
| --- | --- | --- |
| Real libudev monitor creation | NULL, errno97 | Non-null monitor |
| Embedded haptic0x1000 | -1, UDEV failure | 0, exit0 |
| Embedded joystick0x200 | -1, UDEV failure | 0, exit0 |
| Embedded timer+video0x21 | 0, one display | 0, one display |
| Embedded combined0x3221 | -1, UDEV failure, null video | 0, one display |

`physical-v2-before.txt` and `physical-v2-after.txt` retain the full narrow reports. Successful calls can retain an earlier SDL error string; the recorded integer return and video/display fields determine success. The post-fix direct libudev diagnostic exits6 because receiving-enable returns `-EINVAL`; input and sound filter-add calls return0. This remaining BPF limitation is explicit and does not prevent the embedded SDL initialization path, which ignores receiving-enable's result. No hotplug-event delivery is claimed.

`physical-netlink-after.txt` records all 35 native checks passing on build 4446, including idle poll, port binding and reuse, group-mask readback, explicit unsupported-operation failures, and preserved route replies. The source repair is commit `72e1eef8b3125b39e7c852abf2d16bf7a1f82029`. A fresh game launch is in progress; this archive does not yet contain a rendered-frame or gameplay result.

`embedded-probe/embedded-provenance.json` binds the detailed before/after artifacts and source fingerprints. `provenance.json` and `SHA256SUMS` bind the complete current evidence; the common archive manifest and commits are owned by the parent task.
