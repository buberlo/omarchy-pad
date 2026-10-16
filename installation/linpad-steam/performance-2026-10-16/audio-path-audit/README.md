# Hollow Knight audio path audit — physical connection and sink query passed

Build4447 logged an FMOD output-device initialization failure with code33 at 12:12:01.499 UTC. The run was manually stopped at12:13:22 without a visible game frame. Its error text describes an unexpected/internal condition; the number must not be mapped blindly to the current public FMOD enum, whose numeric layout can differ from Unity's embedded version. No precise failing PulseAudio/ALSA operation has been captured yet. Build4448 later reports that even FMOD’s nosound device could not initialize, then continues into MonoManager assembly reloading. This weakens a routing-only diagnosis: a shared FMOD initialization requirement may be failing. The narrow timestamped excerpt is recorded separately and is not a claim that the game rendered.

## Verified local paths and behavior

- LinPad starts native PulseAudio with its own script, a Unix socket at `/tmp/ishaudio/native`, and a stereo48kHz/s16le pipe sink `/tmp/ishaudio/pcm` read by the iOS audio bridge. The configured output sink is `ishaudio`. The microphone is a separate pipe source; the earlier idle-source mitigation is not an output-device repair.
- The desktop session exports `PULSE_SERVER=unix:/tmp/ishaudio/native`. Its `/etc/pulse/client.conf.d/50-ishaudio.conf` also chooses that socket and disables autospawn, shared-memory transport and memfd. The daemon uses no PID file. These are repository configuration facts; the new probe checks current physical path/configuration presence without changing it.
- Both custom game launchers clear inherited `LD_PRELOAD` and replace `LD_LIBRARY_PATH` with the x86 runtime plus its `pulseaudio` subdirectory. They preserve `PULSE_SERVER` if inherited, but do not explicitly establish it. Their actual invoking-shell and eventual game environment are not yet measured by this audit.
- The packaged x86 runtime includes Debian trixie `libpulse0`17, `libpulse-simple.so.0`, `pulseaudio/libpulsecommon-17.0.so` and `libasound.so.2`. Its own `/etc/pulse/client.conf` contains commented defaults; no matching ishaudio drop-in, ALSA Pulse plugin, or ALSA data/configuration file was found in that local runtime. This is an inventory finding, not proof that the game reads that rootfs configuration: FEX's current file-open path attempts `openat2` under its rootfs and falls back to ordinary host paths after failure.
- The exact local UnityPlayer binary contains PulseAudio backend strings, `libpulse-simple.so.0`, `libasound.so.2` and Pulse context API names. This establishes embedded backend support, not which backend this physical run selected. Only selected string names and the binary's existing hash are recorded; no proprietary binary is copied.
- The session's ARM64 `libishaudio-compat.so` is intentionally removed before x86 execution. Its source comments describe historic missing PI-futex and pipe-errno support. Current LinPad implements PI-futex commands and returns `ENOTSOCK` for open non-socket descriptors. Those old comments are not evidence that the x86 game now needs a shim; none has been installed.

## Read-only physical probe

`audio-path-probe` is mirrored in the local serving directory. It prints fixed audio path types/readability, selected invoking-shell audio variables, a whitelist of system Pulse routing/transport settings, and the presence of the exact built-in ALSA-to-Pulse routes. It does not execute audio clients, connect to the server, open audio FIFOs/devices, inspect arbitrary process environments, read cookie files, follow config includes, dump user configuration, send a report or change guest state. It produces stdout only, which the parent operator may redirect using its existing report channel.

```sh
sh /tmp/audio-path-probe
```

A socket node can exist without a live listener. Library presence does not prove successful loading or Pulse connection. These are the limits of this deliberately passive first probe. Its shell-settings output describes the probe's invoking shell, not the running game's environment. Syntax and three synthetic tests against the exact shipped awk filters pass; cookie/password-like fixture fields do not appear in output. This passive probe ran physically on build4447 at12:22:01 UTC: `PULSE_SERVER` and other audio variables were unset in its invoking shell, the custom socket/FIFOs and x86 Pulse/ALSA libraries existed, conventional Pulse socket paths were absent, the host ishaudio client drop-in was present, and the x86 rootfs drop-in/ALSA Pulse plugin/configuration were absent. The report is `physical-paths4447.txt`. Server liveness, actual game environment, successful x86 library loading and audio output remain unproven by that passive result.

## Primary documentation and next evidence boundary

[FMOD Linux platform documentation](https://www.fmod.com/docs/2.04/api/platforms-linux.html) says the default Linux backend uses PulseAudio when available, with ALSA as an alternative. The game embeds its own version, so this general documentation does not prove its selected backend. [FMOD's common API reference](https://www.fmod.com/docs/2.04/api/core-api-common.html#fmod_result) distinguishes internal and output-initialization errors. A historical [FMOD staff response](https://qa.fmod.com/t/error-51-on-linux/16184/2) describes older Pulse detection via `pulseaudio --check`; the current game's binary did not contain that literal command, so that historical mechanism remains unverified here.

Review the passive result first. A later, separately bounded x86 libpulse connect/enumeration probe could establish which configuration is read and whether the native sink is reachable, then an FMOD-specific trace could locate its actual failing call. The bounded active probe passed on physical build4448, as recorded below. No audio correction has been applied. Do not install the historical compatibility shim, force ALSA, or alter Pulse transport solely from the error string.

## Explicit-route diagnostic: physical PASS

`pulse-connect-probe.c` dynamically loads the installed x86 `libpulse.so.0`, creates a Pulse mainloop/context, connects only to `unix:/tmp/ishaudio/native` with `PA_CONTEXT_NOAUTOSPAWN`, and requests the existence of the known `ishaudio` sink. It creates no recording/playback stream and prints no server identity, cookie or arbitrary sink properties. The connection loop is bounded to15seconds, sink query to10seconds, and the wrapper to60seconds plus3seconds kill grace. Library initialization can precede the internal timer, hence the external bound remains important.

The operator would stage these served files under `/tmp/linpad-pulse-probe/`:

| Served file | Target filename |
| --- | --- |
| `pulse-connect-probe` | `pulse-connect-probe` |
| `pulse-probe-client.conf` | `client.conf` |
| `pulse-probe-zero-token` | `zero-token` |

`run-pulse-connect-probe` checks all three hashes before invoking patched FEX. It supports a separate `--negative-missing-socket` control. The private config disables autospawn, shared memory, memfd and default display/localhost connections. It does not replace any system or game configuration.

Credential isolation is explicit. Official PulseAudio17 `client-conf.c` falls back to other cookie locations if a configured cookie read fails; therefore setting `PULSE_COOKIE=/dev/null` would not safely exclude actual cookie access. This diagnostic instead requires a checked256-byte all-zero synthetic token, sets `PULSE_COOKIE` to that file, and scopes `HOME`/`XDG_CONFIG_HOME` to its diagnostic directory before loading libpulse. `DISPLAY` is cleared so X11 properties cannot supply credentials. The synthetic token is not copied from any account or server. The loader may open its own diagnostic token read/write and prepare directories inside that diagnostic scope, but it cannot reach the user's cookie locations through its normal fallback paths. No daemon configuration is changed.

The compiled binary and wrapper passed on physical build4448 at12:31:25–12:31:26UTC. The negative missing-socket control correctly reported connection refusal and exited0 in0.173s. Explicit routing connected and found exactly one `ishaudio` sink, exiting0 in0.376s. This proves x86 Pulse connection and sink enumeration using explicit routing. It would not prove FMOD initialization, emitted PCM, audible sound or gameplay. A separate `fex-hollow-audio-candidate` hook is available for a later controlled game launch after that gate: it sets the same socket/private transport config and checked synthetic token, preserves the game's HOME, and delegates to the existing EGL/FEX hook. It is an unqualified per-process candidate, not an installed fix. Build4448's first baseline launch remains unchanged.

The probe uses public PulseAudio17 API declarations verified against the official tagged `context.h`, `def.h` and `introspect.h` files. Their source hashes and URLs are retained in provenance. The declared context states and callback pointer signatures match those headers. [`pa_mainloop_iterate`](https://freedesktop.org/software/pulseaudio/doxygen/mainloop_8h.html) supplies nonblocking loop iterations; the [PulseAudio environment documentation](https://wiki.freedesktop.org/www/Software/PulseAudio/FAQ/) documents routing/configuration variables. No system PulseAudio development package or new runtime library was installed to build this diagnostic.

## Self-contained operator entrypoint

The parent ran the served `pulse4448-probe` script on physical build4448 after its CPU profile. It downloads the compiled probe, private configuration, synthetic token and hash-checking runner into the dedicated diagnostic directory; runs a missing-socket negative control followed by the explicit-socket query; and POSTs only its small report to the existing LAN report receiver, including on failure. It writes diagnostic payload/report files but does not install a game fix, modify a runtime/system configuration, create an audio stream, or signal the game/daemon. All payload hashes are pinned in the script. It deliberately does not download or run `fex-hollow-audio-candidate`.

The negative control handles both immediate context failure and an asynchronous transition to the failed state. Final source/binary/wrapper hashes are in `provenance.json`; earlier intermediate hashes are superseded. The complete scoped physical result is `physical-pulse4448-pass.txt`. The running game was left unchanged. This folder is frozen at that gate; the separately archived `fmod-sched-repair` diagnosis subsequently reproduced an independent FMOD thread-attribute failure. No PCM playback, audible sound or full-game audio initialization has been verified here.
