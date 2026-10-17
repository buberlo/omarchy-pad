# What finally removed the client's startup assertion

The client died on every start with

```
src/tier0/threadtools.cpp (2526) : Assertion Failed: Permission denied
```

four times in a row, right after the breakpad/error-reporter initialisation, and nothing at the syscall
level failed: the syscall ring (once it was filled correctly) showed only successful calls in the
client's last 96 syscalls, and there was no EPERM anywhere in them. "Permission denied" is Valve's own
text there, not a syscall's errno — which is why prctl, ptrace, setpgid, the scheduler calls and
setrlimit all came up empty.

What located it was logging the filesystem probes: `sys_stat_path`, `sys_faccessat` and `sys_openat`
now print any path containing "errorreporter", "crashhandler" or "breakpad". The client was looking for
its crash reporter in platform directories it had never been given:

```
stat '/opt/steam-host/usr/lib/steamrtarm64/steamerrorreporter'
stat '/opt/steam-host/usr/lib/linuxarm64/steamerrorreporter'
stat '/root/.steam/sdkarm64/steamerrorreporter'
stat '/opt/steam-host/usr/lib/aarch64-linux-gnu/bin/steamerrorreporter'
```

Two of those existed but were unusable glibc binaries; the other two did not exist. Providing all four
(two copied in, two wrapped so they run through the glibc loader) removed the assertion entirely:
six consecutive polls showed `assertion=0` where every previous run had shown 4.

Two notes worth keeping:

- `BVerifyInstalledFiles` checks the *size* of `linuxarm64/steamerrorreporter` (expects 143504) and
  `steamrtarm64/steamerrorreporter` (expects 158240), so those two must be the real binaries; the
  wrappers belong at the paths that are only probed.
- Replacing the reporter with a 379-byte script made the client complain about the size instead of
  asserting — a useful confirmation that these files are the right lever.

The client still does not finish starting; after the assertion was gone a flood of
`Error relocating /lib/libcrypto.so.3: __syslog_chk: symbol not found` appeared, which is the *musl*
loader picking up glibc libraries that a too-broad `/lib` symlink experiment left on the default search
path. Those 314 symlinks were removed again (keeping `/lib/ld-linux-aarch64.so.1`); confirming the
client's state after that cleanup is the first task of the next round.
