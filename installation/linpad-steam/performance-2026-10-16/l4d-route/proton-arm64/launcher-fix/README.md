# The flood: my own launcher fed the glibc library path to a musl interpreter

For hours the iPad's terminal drowned in

```
Error relocating /lib/libcrypto.so.3: __syslog_chk: symbol not found
                                        getcontext / __fdelt_chk / __memcpy_chk / __isoc23_strtol …
```

These are glibc symbols (the `__*_chk` fortify variants, the ucontext API, glibc 2.38's strtol), so a
glibc library was being loaded by the *musl* loader. The source was not Steam at all but
`linpad-proton-arm64`, the Proton launcher:

```sh
export LD_LIBRARY_PATH="$glibc_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec python3 "$P/proton" waitforexitandrun "$@"
```

`python3` in this guest is musl. With `/opt/linpad-glibc` on its library path it loaded the glibc
libcrypto and died with that flood — so Proton never started, which is why the game never came up. The
comment two lines above the export warns about exactly this ("that would break every musl binary in
this shell"); the line did it anyway.

The export is gone. Proton sets wine's own library paths itself (as the comment above also says), so
the interpreter does not need it. Verified on the device: with the line neutralised,
`grep LD_LIBRARY /usr/local/bin/linpad-proton-arm64` shows only comments, and a `l4d-play -g` run
produced `relocating: 0` in the terminal where it previously produced dozens of lines per second.

Also worth knowing when chasing this again: `ishwl-x11` respawns the app it was given, so a launcher
that dies immediately produces an endless loop of these errors, and `steam-local` (the Steam
supervisor) does the same for the client. Both had to be killed before the terminal could be read.
