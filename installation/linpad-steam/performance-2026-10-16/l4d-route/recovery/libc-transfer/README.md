# Repairing the guest's libraries (done, verified — keep for the next time)

While removing the glibc symlinks that caused the endless `libcrypto` flood, the removal ran through
`/lib`, which is a symlink to `/usr/lib` in this image. That deleted three files Alpine needs:

```
/usr/lib/libc.musl-aarch64.so.1     -> symlink to ld-musl-aarch64.so.1   (the C library)
/usr/lib/libcrypto.so.3                                                   (musl build)
/usr/lib/libssl.so.3                                                      (musl build)
```

Every external program stopped starting (`ls`, `ps`, `cat`, `wget`, `tar`), only the in-memory shell
survived. The app's own **Repair cannot fix it** — it unpacks its repair kit inside the guest
("Could not unpack the repair kit in Lin").

## How it was repaired, without a reinstall

1. **The right sources**: the guest is an Alpine minirootfs. `app/GuestARM64.xcconfig` names it:
   `https://dl-cdn.alpinelinux.org/alpine/v3.21/releases/aarch64/alpine-minirootfs-3.21.0-aarch64.tar.gz`
   In Alpine, `libc.musl-aarch64.so.1` is only a symlink to `ld-musl-aarch64.so.1`; the real file is
   `/lib/ld-musl-aarch64.so.1` (723 480 bytes).

2. **Get files into the guest.** Two things matter:
   - The shell may be `sh` (BusyBox) or `bash`. **`sh` has no `/dev/tcp`** — start `bash` first, or use
     a program. `echo $BASH_VERSION` tells you which one you have.
   - **The good way is `wget`** once the libc is back, because it transfers binaries correctly and needs
     no quoting games:
     ```
     wget http://<mac-ip>:9001/libcrypto.so.3 -O /usr/lib/libcrypto.so.3
     wget http://<mac-ip>:9001/libssl.so.3    -O /usr/lib/libssl.so.3
     ```
     Serve with `python3 -m http.server 9001` in a directory holding the files.
   - The fallback that worked for the libc itself (no programs available) is bash `/dev/tcp` plus
     `printf '%b'` over a hex-escaped stream (`xxd -p | sed 's/../\\x&/g'`). Long commands are mangled
     when typed through the on-screen keyboard — keep each piece short.

3. **Order**: libc first (that alone makes every program start again), then libcrypto and libssl.

## Evidence it worked

After the transfer: `ls`, `wc`, `tail`, `cat` and `wget` all run again, and `l4d-play -g` starts Wine and
Proton (the `libcrypto` load in the output is Wine starting up). `ps` still prints nothing — `/proc`
handling is the next thing to check.

## Still open after this repair

- **Separate the library paths**: `linpad-proton-arm64` must not put `/opt/linpad-glibc` on
  `LD_LIBRARY_PATH` for the musl `python3`; Wine needs it, the interpreter must not see it
  (`41c6b09d` has the reasoning).
- **`ishwl-x11`** did not start in the last run, so `/tmp/l4d-game.log` stayed empty; the X wrapper and
  `WAYLAND_DISPLAY` are what to look at next.
