# What the guest needed after the /lib cleanup, and where it stands

The cleanup that ended the `libcrypto` flood removed more than intended. Everything below was repaired
on the device; this is the record, written after the fact because the work lived only in the guest.

## The four libraries

| File | How it came back |
|---|---|
| `/usr/lib/ld-musl-aarch64.so.1` | from the Alpine minirootfs (`libc.musl-aarch64.so.1` is only a symlink to it); streamed with `bash` + `/dev/tcp` + `printf '%b'` over a hex-escaped file |
| `/usr/lib/libc.musl-aarch64.so.1` | same archive, same channel |
| `/usr/lib/libcrypto.so.3` | Alpine's musl build (4 139 216 B), verified byte-identical (`cmp`) before sending |
| `/usr/lib/libssl.so.3` | Alpine's musl build (868 080 B) |

Once the loader was in place the tools started again, and `wget` plus `python3 -m http.server` on the Mac
became the clean channel for the rest (binary, no quoting games).

## The tools that were left broken

`ls`, `cp`, `ln`, `od`, `chmod`, `mv`, `cat` all exited **127** (missing interpreter), while `ps`, `nc`,
`sh`, `wget`, `echo` worked. `chmod` being broken made BusyBox unusable at first. The fix that worked:

```sh
for t in ls cp ln od chmod mv cat; do
    wget -q http://<mac-ip>:9001/busybox -O /bin/$t     # -O keeps the existing exec bit
done
```

BusyBox (v1.37.0, from the Alpine archive) behaves as whichever applet name it is called by, so
overwriting the broken binaries is enough. After that `ls /` and `cp` returned exit 0.

## The control channel that made this possible

The on-screen keyboard mangles long commands, and the flood drowns the output, so nothing was typed that
could be avoided:

- **Mac → guest**: scripts and binaries via `wget` from a `python3 -m http.server` in a scratch dir.
- **Guest → Mac**: results via `nc <mac-ip> 9002 < /tmp/report.txt` (the guest sends, the Mac reads).
- Every diagnostic was a small script; the flood lines were filtered out of the report before sending.

## The helpers, and their permissions

`wget -O` writes 644, so after restoring them the helpers had to be made executable:
`chmod 755 /usr/local/bin/{l4d-go.sh,l4d-play,linpad-proton-arm64,ishwl-x11,steam,steam-local}`.
Without that `l4d-go.sh` failed with `Permission denied` and the game never started.

`guest-tools/l4d-go.sh` and `guest-tools/wine-wrapper` are the two scripts that matter; the wine wrapper
belongs at `<Proton>/files/bin-arm64/wine` with the original renamed to `wine.real`.

## Where the chain stands

```
l4d-go.sh  ->  l4d-play -g  ->  ishwl-x11  ->  Xwayland  ->  Wine  ->  left4dead.exe
   ✔              ✔                ✔            ✗ (exit 127)
```

`ishwl-x11` reports `Xwayland failed to start`. The real error is:

```
Error loading shared library libtirpc-nokrb.so.3: No such file or directory
        (needed by /opt/steam-x11/usr/bin/Xwayland)
```

The glibc interpreter itself is fine (`ld.so --help` works, 672 files in
`/opt/steam-host/usr/lib/aarch64-linux-gnu`, `libc.so.6`/`libdl.so.2`/`libm.so.6` all present). What is
missing is Steam's `-nokrb` variant of libtirpc inside `/opt/steam-x11/usr/lib`. The local Steam archives
carry plain `libtirpc.so.3` (in `steam-lsof.tar.gz`), not that variant — so either Steam's x11 bundle has
to be re-fetched, or the dependency has to be satisfied from another source. That is the last blocker.

## To pick this up again

1. Restart the tooling: WebDriverAgent on the iPad, `python3 collector.py` on the Mac, and a scratch dir
   with `python3 -m http.server 9001`.
2. Rebuild `l4d-go.sh` and the wine wrapper from `guest-tools/` and copy them in.
3. Fix `libtirpc-nokrb.so.3`, then `l4d-go.sh` and read `/tmp/l4d-game.log`.
