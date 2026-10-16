# Later build4448 presentation observations

This fixed snapshot extends the separate runtime follow-up, without changing
the signed build4448 artifact/release proof. The actual game logs requested
960x540 swapchain initialization at 12:31:19.818 UTC, an unload milestone at
12:34:43.558 and Steam initialization at 12:36:15.479. These log messages show
continued startup progress; they do not establish a presented game frame.

The passive device report at 12:35:48 verifies the actual compositor identity:
PID 16 runs `ishwl --runtime-dir /tmp/ishwl -v`, stdout/stderr target
`/tmp/ishwl.log`, and both `/tmp/xdg-runtime/wayland-0` and `wayland-0-gpu`
exist. The frame log still has zero rows, no live `v*.buf` view buffers are
reported, and the compositor log has no mapped toplevel. The later 12:36:50
short report also has zero rows. No game-view frame is qualified here.

Source confirms these paths: `LinuxGUIBridge.swift` launches the compositor
with that runtime/log, while `wl-bridge/src/dmabuf.c` accepts the GPU socket
into the same server/display. There is no separate GPU compositor required
by this implementation. Native DesktopKit/Terminal windows do not necessarily
create guest Wayland views, so their absence from the map log is expected.

The selected-environment section is empty because this LinPad procfs has no
`/proc/PID/environ` entry (`fs/proc/pid.c` implements `cmdline`). It must not
be interpreted as proof that ISHWL_FRAMELOG or the other variables are absent.
The script prints only a small allowed set if that interface exists.

A second bounded CPU profile contains 9,435 samples, 9.435 seconds of aggregate
sample weight over about 5.436 seconds of sampled wall-clock span. Self weight
is 37.997% in `__psynch_mutexwait`, 33.302% in `ss_get` and 21.876% in
`__psynch_mutexdrop`. The selected call paths place most mutex weight under
`ss_get`, supporting investigation of its fixed PC-index lookup/lock cost.
Sample weights are not frame rate, GPU utilization, measured lock hold time
or proof of a deadlock. A subsequent source repair requires a new build.

Separate unresolved startup messages are retained: FMOD could not initialize
an output device, and SteamAPI reports no running Steam instance plus a
missing `/root/.steam/sdk64/steamclient.so`; Steam initialization fails at
12:36:18.827. This snapshot does not assign those messages as the cause of
absent frames and does not claim sound or Steam integration works.

Raw profiler captures, process inventories, complete logs and game binaries
remain local. `provenance.json` contains raw-source hashes and excerpt lineage.
The prepared compositor diagnostic reads regular files/limited metadata and
does not consume bridge FIFOs, send signals or change configuration. Frozen
build4448 release evidence and the earlier runtime-follow-up are unchanged.
