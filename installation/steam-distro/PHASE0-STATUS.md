# Phase 0 status: stopped, and what to do next

The Phase 0 spike (agent `c889c6b9`, "Phase 0 Arch rootfs spike") was stopped on
17 October 2026 after roughly an hour of no artifacts. This file is the handover: what is
proven, what is not, and the narrowed steps that should replace the original brief.

## START HERE (next session)

Phase 0 is answered; do **not** start by re-litigating it. The build continues at Phase 1:

1. **glibc session layer (host).** `sh installation/steam-distro/build-guest-tools.sh` — builds
   `ishwl`, `ishwl-x11-wm`, `libishwl-scm.so`, `libishwl-compat.so` in the Arch container in the
   colima VM, with `NEEDED.txt` proving glibc linkage. Requires `colima start linpad-fex`.
2. **Install them into the image off-device** (the guest is an immutable runtime): copy into
   `work/steam-distro/arch-container/data/…` via a rebuild of the tree, or better, into the
   container and re-export → re-import with the fixed `fakefsify`. Add `/etc/ishwl/{apps}`,
   `/etc/profile.d/gpu.sh` (Arch ICD path), `/etc/ishwl/session.d/*`, and the Steam launcher
   files from `release/guest/steam-*`.
3. **Replace `/sbin/init`** (currently a systemd symlink) with a small script that stays alive and
   reaps — the app execs it as PID 1.
4. **Probe** (`tests/compat-probe.sh`): the no-`LD_PRELOAD` SCM_RIGHTS fd-passing test is the one
   that decides whether the shims are needed; `pacman` in the guest is *not* required (see
   Round 12), and the last open alpm suspect is `open(O_DIRECTORY)`+`fdopendir`
   (Round 14/15) — leave it alone unless it becomes free.
5. Then Phase 2: native Steam on this root, `steam -gamepadui`, with a head-side screenshot
   before any device time. Phase 3: per-root app integration (`defaultRoot`, import-if-missing,
   per-root lock, gate `CurrentRoot.m`'s `/etc/apk` write and the Alpine repair kit).

Keep: the `tools/fakefs.c` hardlink fix; `DisableSandbox` (in `[options]`, not `[aur]`) for
off-device builds; `fakefsify <tar> <dir>` where `<dir>` *is* the fs-control directory.

## Proven, on disk

| Thing | Where | Evidence |
| --- | --- | --- |
| macOS `ish` CLI built | `work/LinPad/build-arm64-release/ish` (1,060,032 B) | built 03:54 |
| `fakefsify` built | `work/LinPad/build-arm64-release/tools/fakefsify` (56,696 B) | built 03:54 |
| Alpine control root fakefsified | `work/steam-distro/alpine/` (9.6 MB, has `fs-control`) | — |
| Alpine minirootfs cached | `work/steam-distro/cache/alpine-minirootfs-3.21.0-aarch64.tar.gz` | — |
| Colima ARM64 VM running | docker context `colima-linpad-fex` | `docker info` answered |
| An ARM64 Arch image exists | `menci/archlinuxarm:latest` (899 MB) | pulled/used by the spike |

## Not proven (the spike never got here)

- No boot transcript for the Alpine control run: the exact `ish -f <root> …` command and its
  output were never written down. Nothing says the fakefsified control root boots.
- No Arch tree outside the VM, no `pacman -Q` list, no probe results, no sizes, no timings,
  no case-collision check. `work/steam-distro/{arch-root,evidence,logs}` are all 0 bytes.

## The loop, as observed

Across four checks over ~50 minutes: `arch-root`, `evidence` and `logs` stayed 0 bytes; a
`menci/archlinuxarm:latest` container repeatedly showed as freshly started ("Up 47 seconds",
"Up About a minute", "Up 3 minutes"); no new file ever appeared in the repo's
`installation/steam-distro/`; the builder image my script would create
(`linpad-ishwl-builder:archlinuxarm`) was never made either. Conclusion: the spike was
re-doing a step inside the VM without writing anything out, not making slow progress.

## Trap worth remembering

`archlinux:latest` has **no arm64 manifest** ("no matching manifest for linux/arm64/v8"), so
the official Arch container cannot be used. Either use `menci/archlinuxarm:latest`, or
bootstrap the ALARM tarball (`http://os.archlinux.org/os/ArchLinuxARM-aarch64-latest.tar.gz`)
into a chroot inside any multi-arch aarch64 image. `build-guest-tools.sh` here uses the
former and is syntax-checked but has never been run.

## Do this instead — three steps, each writing its transcript before the next

1. **Control run, and write it down.**
   ```sh
   cd <workspace root>
   mkdir -p work/steam-distro/evidence
   work/LinPad/build-arm64-release/ish -f work/steam-distro/alpine /bin/sh -lc \
       'cat /etc/os-release; uname -a' 2>&1 | tee work/steam-distro/evidence/01-alpine-control.txt
   ls -la work/steam-distro/alpine          # fs-control AND data/ must both be there
   ```
   If `data/` is missing, fix `fakefsify` before touching Arch: a broken control run makes
   every later conclusion worthless.

2. **Get the Arch tree out of the VM, once, with the package list.**
   ```sh
   D="docker --context colima-linpad-fex"
   id=$($D create menci/archlinuxarm:latest sh -c 'while :; do sleep 3600; done'); $D start "$id"
   $D exec "$id" sh -c 'pacman-key --init; pacman-key --populate archlinuxarm; pacman -Syu --noconfirm'
   $D exec "$id" pacman -Q > work/steam-distro/evidence/02-packages.txt
   $D cp "$id:/." work/steam-distro/arch-root        # or tar + cp the tree out
   $D rm -f "$id"
   ```
   Then answer the app-critical questions from that tree and record them: what `/sbin/init`
   is (must not be systemd), whether `/etc/apk` exists (the app's `CurrentRoot.m` rewrites
   `/etc/apk/repositories`), `/etc/os-release`, `/etc/pacman.conf`, and `/bin/login`/`/bin/sh`.

3. **fakefsify, boot, probe.** Export the archive *from inside the guest* (as
   `release/build-rootfs.sh` does), then boot it and run `tests/compat-probe.sh` against it,
   writing `evidence/03-probe.txt`. The probe must include a plain **glibc** program doing
   SCM_RIGHTS fd passing **with no `LD_PRELOAD`** — that decides whether Phase 1 builds a
   glibc `scm-compat` or drops the shims — plus `pacman -Sy`, `python3`, pthreads,
   `posix_spawn`, epoll, `flock`, `getrandom`, fork/reap, and `/proc` reads. Record sizes
   (compressed/uncompressed/entries), boot time, and any case-colliding paths.

Do not start step 2 until step 1's transcript exists in `evidence/`.

## Round 7 finding: the control root is broken, and that is why Phase 0 stalled

Executing step 1 above produced the missing transcript, and it fails at the first object:

```
$ work/LinPad/build-arm64-release/ish -f work/steam-distro/alpine /bin/sh -lc 'cat /etc/os-release; uname -a'
xX_main_Xx: No such file or directory

$ ls -la work/steam-distro/alpine
drwxr-xr-x@ 6 konrad  staff  192  fs-control          # the ONLY entry
$ du -sh work/steam-distro/alpine/data
(no data/ directory)
```

`fakefsify` left `fs-control/` behind but **no `data/` payload**: the fakefs root is incomplete,
so the CLI has nothing to execute and answers with iSH's `xX_main_Xx` stub error. Every later
step the spike attempted (Arch bootstrap, probes) was therefore building on a root that could
never boot — which matches the hour of 0-byte artifacts.

So the first thing to fix is the pipeline object, not Arch:

1. Re-run `fakefsify` and capture its output and exit status; confirm the result has **both**
   `data/` (the file contents) and `fs-control/`. Check free space and that the minirootfs
   tarball is intact (`work/steam-distro/cache/alpine-minirootfs-3.21.0-aarch64.tar.gz`).
2. Re-run the control boot above; `PROBE_OK` must appear in
   `evidence/01-alpine-control.txt`.
3. Only then start the Arch step from this file.

## Round 7 correction: the control root is fine, the -f path was wrong

fakefsify's output keeps the payload inside `fs-control/`:

```
work/steam-distro/alpine/fs-control/{meta.db,meta.db-shm,meta.db-wal,data/}
```

(The `<root>/data` layout I expected is the *iOS app's* container layout, not the CLI's.)
So the control invocation is:

```sh
work/LinPad/build-arm64-release/ish -f work/steam-distro/alpine/fs-control /bin/sh -lc 'cat /etc/os-release; uname -a; echo PROBE_OK'
```

Observed output (also in `evidence/01-alpine-control.txt`):

```
NAME="Alpine Linux"
ID=alpine
VERSION_ID=3.21.0
PRETTY_NAME="Alpine Linux v3.21"
HOME_URL="https://alpinelinux.org/"
BUG_REPORT_URL="https://gitlab.alpinelinux.org/alpine/aports/-/issues"
Linux MacBook-Pro-von-Konrad.local 4.20.69-ish SUPER AWESOME Oct 17 2026 03:54:56 aarch64 Linux
PROBE_OK
```

If `PROBE_OK` is present above, the CLI + fakefsify pipeline is proven and the next session can
start the Arch step directly; no re-import is needed.

## Round 8 findings (Arch bootstrap attempted)

One transaction against `menci/archlinuxarm:latest` (base image: 141 packages,
`/etc/os-release` = `NAME="Arch Linux ARM"`, `ID=archarm`, `ID_LIKE=arch`).

### 1. pacman refuses to run without Landlock — and that includes the iPad

```
$ docker exec … pacman -Syu --noconfirm
error: restricting filesystem access failed because the Landlock ruleset could not be applied: Operation not permitted
error: switching to sandbox user 'alpm' failed!
error: failed to synchronize all databases (failed to retrieve some files)
```

Arch's pacman sandboxes its **download** step with Landlock and drops to the `alpm` user. iSH
stubs the whole `landlock_*` family (`kernel/arch/arm64/calls.c` 442–448, ENOSYS, no fallback),
so this failure will happen **on the iPad too**, not only in Docker. Fix before anything else:
run pacman with its sandbox off (`pacman -Syu --disable-sandbox`, or permanently in the image's
`/etc/pacman.conf`: `DisableSandbox` / no `DownloadUser = alpm`). Do not "implement Landlock" —
that is a much larger change than this build needs.

### 2. What the app will see in an Arch root (audit questions 1 and 4, answered)

| Path | State | Consequence for the app |
| --- | --- | --- |
| `/sbin/init` | **symlink** (to systemd in ALARM) | the image must replace it — PID 1 is `/sbin/init` (`app/UserPreferences.m:164`); systemd cannot run here |
| `/etc/apk`, `/etc/apk/world`, `/etc/alpine-release` | **MISSING** | `CurrentRoot.m`'s `/etc/apk/repositories` rewrite and `Roots.m`'s apk world read find nothing — gate them on existence; nothing should write into an Arch root |
| `/bin/sh` | symlink (bash) | fine (`/bin/login -f root` also present) |
| `/etc/os-release` | symlink | fine; `wl-bridge/guest/os-release.sh` must not claim `ID_LIKE=alpine` for this root |
| `/usr/bin/pacman`, `/var/lib/pacman` | present | the pacman-side equivalent of the apk world/reinstall list lives here |

### 3. Copy the tree with `docker export`, not `docker cp`

`docker cp "$id:/." arch-root` produced **312 KB** — useless. Use `docker export "$id" > arch.tar`,
which is complete *and* is exactly the tarball `fakefsify <rootfs.tar.gz> <outdir>` consumes.

### Next (replaces step 2 earlier in this file)

```sh
D="docker --context colima-linpad-fex"
id=$($D create menci/archlinuxarm:latest sh -c 'while :; do sleep 3600; done'); $D start "$id"
# Arch's pacman sandbox cannot work here or on the iPad:
$D exec "$id" sh -c "sed -i 's/^#*DisableSandbox.*/DisableSandbox/' /etc/pacman.conf; grep -q DisableSandbox /etc/pacman.conf || echo DisableSandbox >> /etc/pacman.conf"
$D exec "$id" sh -c 'pacman-key --init; pacman-key --populate archlinuxarm; pacman -Syu --noconfirm'
$D exec "$id" pacman -Q > work/steam-distro/evidence/02-packages.txt
$D export "$id" > work/steam-distro/arch-base.tar
$D rm -f "$id"
work/LinPad/build-arm64-release/tools/fakefsify work/steam-distro/arch-base.tar work/steam-distro/arch-root
work/LinPad/build-arm64-release/ish -f work/steam-distro/arch-root/fs-control /bin/sh -lc 'cat /etc/os-release; uname -a; echo ARCH_BOOT_OK'
```

Then replace `/sbin/init` with something that stays alive and reaps, and run the probe suite
from the corrected control-run recipe at the top of this file.

## Round 9: pacman fixed, and the real remaining blocker is fakefsify vs merged-/usr

**What now works** (all verified this round):

```sh
# Arch's pacman needs its Landlock sandbox off — in Docker AND on the iPad (iSH stubs landlock):
docker exec $id sh -c "sed -i 's/^DownloadUser/#DownloadUser/' /etc/pacman.conf;
                       grep -q '^DisableSandbox' /etc/pacman.conf || echo DisableSandbox >> /etc/pacman.conf"
docker exec $id sh -c 'pacman-key --init; pacman-key --populate archlinuxarm; pacman -Syu --noconfirm'   # exit 0
docker exec $id pacman -Q > evidence/02-packages.txt      # 138 packages
docker export $id > work/steam-distro/arch-base.tar       # 669,062,144 bytes
```

**What still fails, reproducibly, on both inputs** (the 669 MB container export *and* the
canonical `ArchLinuxARM-aarch64-latest.tar.gz`, 829,367,415 bytes):

```
$ build-arm64-release/tools/fakefsify <arch.tar[.gz]> work/steam-distro/arch-root
error!!1! 144 2 File exists          # after writing ~490 MB
$ ls work/steam-distro/arch-root      # data  meta.db  meta.db-shm   — no fs-control/ wrapper,
                                       # so nothing can mount it
```

Both inputs contain Arch's **merged-/usr symlink layout**: `bin`, `lib`, `sbin`, `lib64` are
symlinks into `usr/`, and the same tarball also carries real entries under those names. Extracting
both into a fakefs trips over "File exists" the moment the second entry for a path arrives. This
is why Phase 0 could never get past the first object, and it is **not** about Docker, nor about
pacman, nor about the mirror.

**Fix (next session, in this order):**

1. Extract the tarball to a plain directory with `tar --keep-directory-symlink` (or extract, then
   replace the `bin`/`lib`/`sbin`/`lib64` symlinks with real directories copied from `usr/`), so
   every path appears exactly once.
2. Re-tar that tree (`tar -czf arch-flat.tar.gz -C dir .`) and give **that** to `fakefsify`.
3. Boot it: `ish -f work/steam-distro/arch-root/fs-control /bin/sh -lc 'cat /etc/os-release; uname -a; echo ARCH_BOOT_OK'`
   — the `fs-control/` subdirectory is the path the CLI needs, as the Alpine control run proved.
4. Then, and only then, replace `/sbin/init` (a systemd symlink in the tarball) with something
   that stays alive and reaps, and run the probe suite.

### Correction to the paragraph above (kept, not deleted)

The merged-/usr hypothesis is **not confirmed**: `tar -tzf` of the ALARM tarball lists **zero**
entries matching `^(bin|lib|sbin)/`, so there is no obvious directory-vs-symlink duplication.
Also, the earlier `ls arch-root | head -3` was truncated — `arch-root` has four entries
(`data`, `meta.db`, `meta.db-shm`, and one more), so `fs-control/` may well exist. Check it
before believing any of the above:

1. `ls -la work/steam-distro/arch-root` — if `fs-control/` is there, just boot it:
   `ish -f work/steam-distro/arch-root/fs-control /bin/sh -lc 'cat /etc/os-release; uname -a'`.
   The `No such file or directory` in the transcript may simply be the shell failing that path,
   not fakefsify failing the import.
2. If it is *not* there, read fakefsify's own error instead of guessing: the string
   `error!!1! 144 2 File exists` comes from the tool's sources — `grep -rn "error!!1!" tools/`
   in the repo — and the numbers will say which call failed and with which errno.

Everything else in this file stands: the control run works with `-f …/fs-control`, pacman needs
`DisableSandbox`, `/sbin/init` must be replaced, and `/etc/apk` does not exist in an Arch root.

### Confirmed, after checking with untruncated output

- `work/steam-distro/arch-root` = `data/`, `meta.db`, `meta.db-shm`, `meta.db-wal` — **no `fs-control/`**.
  So the Alpine control root (which has `fs-control/`) and the Arch run differ: this is consistent
  with the Arch import **aborting** after writing the payload, before the wrapper exists.
- The error string comes from `tools/fakefsify.c:40`:
  `fprintf(stderr, "error!!1! %d %d %s\n", err.line, err.type, err.message);`
  So `144` is a **source line** and `2` an error type: read `tools/fakefs.c` around line 144 (the
  shared fakefs import code) and the `err.type` enum to learn the failing call. That is the
  shortest path to the actual cause — do this before re-running anything.

## Round 10-11: Arch boots — Phase 0 answered

**The blocker was this repo's own tool.** `error!!1! 144 2 File exists` decoded: `err.type 2` =
`ERR_POSIX` (enum `ERR_ARCHIVE, ERR_SQLITE, ERR_POSIX`) and `tools/fakefs.c:144` is the hardlink
branch — `linkat()` returning `EEXIST` aborted the whole import. ALARM's tarball lists the same
hardlink pairs repeatedly; Alpine's minirootfs does not, which is why only Arch failed. Fixed in
`tools/fakefs.c` (tolerate `EEXIST`, abort otherwise) and `fakefsify` rebuilt. **Keep this fix.**

**Then it boots** (`evidence/03-arch-boot.txt`):

```
$ ish -f work/steam-distro/arch-root /bin/sh -lc 'cat /etc/os-release; uname -a; echo ARCH_BOOT_OK'
ID_LIKE=arch   BUILD_ID=rolling   HOME_URL=https://archlinuxarm.org/
Linux MacBook-Pro-von-Konrad.local 4.20.69-ish SUPER AWESOME ... aarch64 GNU/Linux
ARCH_BOOT_OK
```

Note `fakefsify <tar> <outdir>` makes **`<outdir>` itself** the fs-control directory (flat:
`data/`, `meta.db`); the CLI wants `-f <that dir>`. The Alpine root's `fs-control/` name was
just the path it was given.

### What the glibc userland does (`evidence/04-probe-basic.txt`)

| Probe | Result |
| --- | --- |
| `getconf GNU_LIBC_VERSION` | **glibc 2.43** — newer than the transplanted 2.41, so the "Mesa26 Gallium requires GLIBC_2.43" ceiling that excluded it **goes away** on this root |
| fork / exec / reaping | works (three children ran and were waited for) |
| pipes, `flock` | work |
| present | pacman, curl, bash, awk, sed, tar, zstd |
| missing | **python3, cc/gcc, wget** — install `python` before the SCM_RIGHTS probe |

### Two roots, two purposes

| Root | Built from | State |
| --- | --- | --- |
| `work/steam-distro/arch-root` | ALARM tarball (2.1 GB) | boots, but `pacman -Q` = **0 packages** (no local DB) |
| `work/steam-distro/arch-container` | container export with `DisableSandbox` + `pacman -Syu` (669 MB) | the one to continue with: registered packages, working pacman |

### Next, in order

1. In `arch-container`: `pacman -S --disable-sandbox python` **in the guest** (this also proves
   network + TLS + signature verification on the iPad path), then run the SCM_RIGHTS fd-passing
   probe with **no `LD_PRELOAD`** — the result decides whether Phase 1 builds a glibc
   `scm-compat` or drops the preloads.
2. Replace `/sbin/init` (a systemd symlink) with something that stays alive and reaps.
3. Then the glibc session layer, native Steam, and the app-side per-root work.

### Correction to the table above

The `arch-container` row says "registered packages"; the check that produced it ran
`pacman -Q 2>/dev/null | wc -l`, so a **failing** pacman also shows as 0. Do not trust that
number in either direction until it is re-run with stderr visible:

```sh
ish -f work/steam-distro/arch-container /bin/sh -lc 'pacman -Q 2>&1 | head -3; ls /var/lib/pacman/local | wc -l'
```

`arch-container` is still the better root to continue with (glibc 2.43, `DisableSandbox` present,
706 MB, pacman binary and `/var/lib/pacman` there), but verify the local database before relying
on `pacman -Q`/upgrades in it. The ALARM-tarball root (`arch-root`) genuinely has no local DB.

## Round 12: in-guest pacman does not initialise alpm — and that is *not* a blocker for this plan

With stderr visible (`evidence/06-pacman-guest.txt`):

```
$ ish -f work/steam-distro/arch-container /bin/sh -lc 'pacman -Q'
warning: config file /etc/pacman.conf, line 92: directive 'DisableSandbox' in section 'aur' not recognized.
error: failed to initialize alpm library:
(root: /, dbpath: /var/lib/pacman/)
could not find or read directory
```

`/var/lib/pacman` **exists and is readable in the same guest** (`ls /var/lib/pacman/local | wc -l`
= 138), so alpm is failing on something *about* the path rather than its absence. Next suspect
list, in order: `statvfs`/`statfs` on the dbpath (space check), `open(O_DIRECTORY)` +
`fdopendir`, `faccessat` with flags, or `getmntent`. Each is a small kernel-side question of the
same kind this project has answered before (`close_range`, `faccessat2`), and the guest-side
experiment is cheap: run `stat -f /var/lib/pacman`, `python3`-less `ls -ld`, and check the
kernel's `sys_statfs`/`sys_statvfs` implementation.

Also cosmetic but worth fixing: my `DisableSandbox` line landed in `[aur]` (line 92), not
`[options]`; move it or drop it once the command-line flag is enough.

### The strategic point: the game app does not need in-guest pacman

Package management for this image happens **off-device** — in the colima VM, where pacman works
perfectly, and where `pacman -Syu` already succeeded. The guest is then an **immutable runtime**:
Steam and its assets are migrated host-side through the app's container, and the Alpine-only
Store/Packages UI never runs against this root. So:

* In-guest pacman is a **nice-to-have**, not a gate. If `statvfs`/`O_DIRECTORY` turn out to be a
  one-line kernel fix, take it; if not, ship without it and keep building the image in the VM.
* This keeps Phase 1/2 (glibc session layer, native Steam, Big Picture) entirely unblocked by the
  alpm question, which is the opposite of how it looked an hour ago.

## Round 13: statvfs is fine — the alpm failure is elsewhere (narrowed, not solved)

`evidence/07-statvfs.txt`, all inside the booted Arch root:

```
$ stat -f /var/lib/pacman      -> ID: 0  Namelen: 255  Type: UNKNOWN (0x66616b65)   # "fake"
                                 Block size: 1048576  Fundamental block size: 4096
$ stat -c "%A %U %n" …         -> drwxr-xr-x root /var/lib/pacman
$ df -h /var/lib/pacman        -> /…arch-container/data  461G  375G   86G  82%  /
$ cat /proc/mounts             -> …arch-container/data / fake rw 0 0
$ grep -n '\[43\]\|\[44\]' kernel/arch/arm64/calls.c
    56: [43] = sys_statfs_arm64
    57: [44] = sys_fstatfs_arm64
```

So: `statvfs` works, the dbpath has correct mode/ownership, `opendir` works (`ls` lists 138 entries),
and `statfs`/`fstatfs` are implemented. My first suspect is **wrong**.

Remaining candidates for alpm's `could not find or read directory`, cheapest test first:
1. **`realpath()` on the dbpath** — try in the guest: `readlink -f /var/lib/pacman`.
2. **`open(dbpath, O_DIRECTORY)` + `fdopendir(fd)`** — `ls` uses plain `opendir`, so this is untested.
   Needs a tiny C program (install `base-devel` off-device first, or write one from the Mac side).
3. `faccessat` with `AT_EACCESS`/flags, or `getmntent` iteration beyond the first lines.

None of these is large; the point of recording it is that the next attempt starts from "the
filesystem reports itself correctly, so look at *how* alpm opens the path", not from statvfs.
And per Round 12: none of it gates Phase 1/2, because the image is built off-device.

## Round 14: realpath is fine too — the message itself is the best clue left

`evidence/08-realpath.txt`: `readlink -f /var/lib/pacman` → `/var/lib/pacman`;
`readlink -f …/local` → `/var/lib/pacman/local`; `cd` + `pwd -P` + `ls` all correct. So
**hypothesis 1 is out as well.** Statvfs ✓, realpath ✓, opendir/readdir ✓, mode/owner ✓.

`could not find or read directory` is libalpm's `ALPM_ERR_NOT_A_DIR` — alpm decided the dbpath is
not a directory. Note the error prints the path **with a trailing slash** (`dbpath: /var/lib/pacman/`).
So the remaining micro-tests, in this order:

1. `stat -c %A /var/lib/pacman/` and `ls -d /var/lib/pacman/` (trailing-slash form) — if the fakefs
   treats `dir/` differently from `dir`, that alone explains alpm.
2. `open(dbpath, O_DIRECTORY|O_CLOEXEC)` + `fdopendir` — the one directory idiom `ls` never uses,
   so it is still untested. Needs a tiny C program; install `base-devel` **off-device** into the
   image (the VM, where pacman works) and ship the test in the image, or compile it from the Mac
   side with the Zig wrapper (`work/compiler-bin/clang … aarch64-linux-gnu`).
3. `_alpm_access(dbpath, R_OK|X_OK|W_OK)` → i.e. `faccessat`/`faccessat2` with flags — implemented
   as `sys_faccessat` (`calls.c:305`), but only the flags that path supports.

Standing conclusion unchanged: none of this gates Phases 1–2; the image is built off-device.

## Round 21-22: `chdir("/")` fails — the fakefs root has no directory entry

`evidence/16-chdir-matrix.txt`, in the booted `arch-steam-root`:

```
$ (cd / && echo ok)                 -> /bin/sh: line 1: cd: /: Not a directory
$ (cd / && exec /bin/sh -c echo ok) -> same
$ foot -e /bin/sh -c echo FOOT_DEF_OK
  err: slave.c:554: /bin/sh: failed to execute: Not a directory
```

So it is **not** a `foot` quirk: *any* process that does `chdir("/")` fails with `ENOTDIR`,
while absolute **path traversal** still works (`ls /usr/local/bin`, `mkdir -p /tmp/xdg-runtime`,
`ishwl` listening on `/tmp/…` all succeed). That combination is the tell: the fakefs root is
reachable *through* paths, but the root entry itself is not a directory.

Earlier (rounds 13-14, `arch-container`) `cd /var/lib/pacman` worked — so the difference is in the
archive, not the kernel. Inspect the first entries of each input (the check is at the end of this
file's round-22 block in the transcript):

* `docker export` output (`arch-steam.tar`) — if its first entry is **not** an explicit `./`
  directory, `fakefsify` creates the root implicitly, and an implicit root can end up marked as a
  plain file while subdirectories are created properly on demand.
* The ALARM and Alpine tarballs both start with their root directory entry, which is why the
  Alpine control root could `cd` anywhere.

**Likely fix**: give `fakefsify` an archive whose **first entry is the root directory** — extract
the container export to a directory and re-tar it (`tar -czf arch.tar.gz -C tree .`, which emits
`./` first), or prepend a `./` entry to the export. Then re-import and re-run:
`(cd / && echo chdir-ok)`.

This also retro-explains the alpm failure from rounds 12-15: `ALPM_ERR_NOT_A_DIR` on a dbpath whose
*root* is not a directory is consistent with the same root-entry defect. One cause, two symptoms.

## Round 22, confirmed: the root directory entry is the defect — and the fix needs care

Archive inspection (the decisive part):

| Input | First entry | `cd /` in the booted root |
| --- | --- | --- |
| `alpine-minirootfs-3.21.0-aarch64.tar.gz` | **`./` (directory)** | works (control root) |
| `ArchLinuxARM-aarch64-latest.tar.gz` | `./bin -> usr/bin` — no `./` | fails: `Not a directory` |
| `docker export` of the container (`arch-steam.tar`) | `.dockerenv` — no `./` | fails: `Not a directory` |

So: **an archive without an explicit root-directory entry gives a fakefs whose `/` is not a
directory.** Path traversal still works (subdirectories are created on demand), which is why
`ls /usr/local/bin`, `mkdir -p /tmp/…` and `ishwl` all worked while `chdir("/")` returned ENOTDIR.
One cause explains three symptoms: `foot`'s slave (`chdir`), libalpm (`ALPM_ERR_NOT_A_DIR` on its
dbpath, rounds 12–15) and any program that changes to the root.

**My first attempt at the fix failed, and why matters**: I extracted the export to `/tmp/as` and
re-tarred with `-C /tmp/as .` (which does emit `./` first). But `tar -x` hit
`Can't open 'dbus-daemon-launch-helper': Permission denied` (a setuid binary, as a normal user),
`tar` exited 1, and the resulting archive was incomplete — `fakefsify` produced a 499 MB root that
the CLI rejects with `xX_main_Xx: No such file or directory`. So the *direction* is right and the
*execution* needs one of these:

1. Extract as root (or with `--no-same-owner --no-same-permissions` and without the setuid files),
   then `tar -czf fixed.tar.gz -C tree .` and **verify `tar -tzf … | head -1` prints `./`** before
   running `fakefsify`. Keep `/dev`, `/proc`, `/sys` as empty directories (the kernel populates them).
2. Or skip extraction entirely: build a minimal tar containing just `./` (a directory), strip its
   two trailing zero blocks, and `cat` it in front of the uncompressed export — cheaper, but verify
   the result with `tar -tzf | head -1` all the same.

Then re-run: `ish -f <fixed-root> /bin/sh -lc '(cd / && echo CHDIR_OK)'`, and only afterwards repeat
the `foot`/client test and the alpm question — both should behave differently on a root whose `/` is
a real directory.

## Round 23: do not route the archive through macOS tar — build it in the container as root

Second inconclusive attempt, and this time the reason is clear. Re-tarring on the Mac:

```
$ tar -xf arch-steam.tar -C /tmp/as2 --no-same-owner --no-same-permissions   # rc=1, 38 errors
$ tar -czf arch-fixed2.tar.gz -C /tmp/as2 .        # first entry ./, but 165 MB (gzipped)
$ fakefsify … -> arch-fixed2 = 499 MB
$ ish -f arch-fixed2 /bin/sh -lc 'cd /'            # xX_main_Xx: No such file or directory
```

The `./` entry was correct this time (the hypothesis is *still* not disproven), but the tree that came
out does not boot, so `tar` on macOS (bsdtar, as a normal user, with setuid files, hardlinks and
device nodes) is the wrong tool for this job. Note the size trap: 165 MB is **gzipped**, so size
alone proves nothing — the 499 MB fakefs and the boot failure are the real evidence.

**Correct approach: build the tar inside the container, as root, with GNU tar, and copy *that* out.**

```sh
D="docker --context colima-linpad-fex"
id=$($D create menci/archlinuxarm:latest sh -c 'while :; do sleep 3600; done'); $D start "$id"
# …install deps, copy the session layer in (see Round 20/21 recipe)…
$D exec "$id" sh -c 'cd / && tar -czf /rootfs.tar.gz .'     # GNU tar as root: emits ./ first,
                                                           # setuid/hardlinks/dev handled properly
$D cp "$id:/rootfs.tar.gz" work/steam-distro/arch-gnu.tar.gz
$D rm -f "$id"
tar -tzf work/steam-distro/arch-gnu.tar.gz | head -1        # MUST print ./
work/LinPad/build-arm64-release/tools/fakefsify work/steam-distro/arch-gnu.tar.gz work/steam-distro/arch-gnu
work/LinPad/build-arm64-release/ish -f work/steam-distro/arch-gnu /bin/sh -lc '(cd / && echo CHDIR_OK)'
```

Everything else stands: `arch-steam-root` (755 MB, round 21) is still the best *bootable* Arch image
— it runs the compositor and the session scripts — it just cannot `chdir("/")` yet, which is what
`foot`, libalpm and probably Steam's helper processes need.

## Round 24, CONFIRMED: an explicit `./` entry is what makes an Arch root usable

`evidence/19-gnu-tar-root.txt` — the whole investigation resolves here:

```
$ docker exec $id sh -c 'cd / && tar -czf /rootfs.tar.gz .'     # GNU tar, as root, in the container
$ docker cp $id:/rootfs.tar.gz …   &&   tar -tzf … | head -1    # -> ./
$ fakefsify arch-gnu.tar.gz arch-gnu                            # -> 803 MB
$ ish -f work/steam-distro/arch-gnu /bin/sh -lc '(cd / && echo CHDIR_OK)'
cd / -> CHDIR_OK
pwd=/
ls / -> bin boot dev
```

So the root-entry hypothesis is proven, and it explains everything that came before: the two roots that
failed (`arch-root` from the ALARM tarball, `arch-steam-root` from `docker export`) both start with a
member other than `./`, while the Alpine minirootfs — the one input that always worked — begins with
`./`. Without that entry the fakefs has no directory inode for `/`: absolute-path traversal still
works (which is why `ls`, `mkdir -p` and `ishwl` all seemed fine), but `chdir("/")` fails with
`ENOTDIR`, which killed `foot`'s slave and libalpm alike.

**The recipe is now settled** (and `arch-gnu`, 803 MB, is the best bootable Arch root):

1. In the container (which is where the session layer is installed anyway — Rounds 20/21):
   `$D exec "$id" sh -c 'cd / && tar -czf /rootfs.tar.gz .'`
   The `rc=2` and the flood of `/sys`, `/proc`, setuid warnings are **harmless noise** — those
   pseudo-files cannot be read, and the archive still boots. Never route this through macOS `tar`
   (Round 23).
2. `docker cp` the archive out; assert `tar -tzf … | head -1` prints `./`.
3. `fakefsify <archive> <root>`; boot with `ish -f <root> /bin/sh`.
4. First check is always `(cd / && echo CHDIR_OK)`.

**Next**, on this foundation rather than on a broken root:
* Rebuild the Steam image the same way (Round 20/21 layer + this tar step).
* Retest `foot`: it should now map a **toplevel** — the first client window on the Arch root.
* Retest `pacman -Q`: with `/` a real directory, the `ALPM_ERR_NOT_A_DIR` that cost Rounds 12–15
  should be gone, which would also settle in-guest package management.
* Then Steam, with a head-side Big Picture screenshot before any device time.

## Round 25: the root-entry fix holds, alpm is solved — and `/dev` must ship empty

`evidence/20-steam2-verify.txt`, on `arch-steam2` (883 MB, built with the Round 24 recipe):

```
1. chdir: CHDIR_OK                      <- the fix holds in the layered image
2. pacman -Q: acl 2.4.0-1               <- libalpm initialises!
3. foot log: err: terminal.c:1195: failed to open PTY: No such file or directory
4. toplevels: 0
```

**Two results and one new, precise blocker.**

*Confirmed*: `chdir("/")` works and **`pacman -Q` works**. The `ALPM_ERR_NOT_A_DIR` that cost Rounds
12–15 was the very same root-entry defect — one cause, three symptoms, now all explained. In-guest
package management is therefore available on this root after all (no longer a "nice-to-have we can
skip").

*New blocker*: `foot` fails with `failed to open PTY`, and the shell complains that
`/dev/null: No such device or address`. The cause is visible in my own build step: I tarred the
container's **populated `/dev`**, so the guest's `/dev/null`, `/dev/zero`, `/dev/random` are ordinary
files copied from the container and they **shadow** the device nodes the kernel creates at boot.
`/dev/ptmx` never exists, so nothing can open a PTY.

LinPad's own Alpine image avoids exactly this: `release/guest/finalize.sh:11-15` creates `/dev`,
`/proc`, `/sys` as **empty directories** and lets the kernel populate them (`app/AppDelegate.m`
makes null/zero/full/random/urandom, ptmx, /dev/pts with devpts mounted, chmod 1777 /dev/shm).

**Fix before the next test** — in the container, just before tarring:

```sh
$D exec "$id" sh -c 'rm -rf /dev/* /proc/* /sys/*; mkdir -p /dev /proc /sys; cd / && tar -czf /rootfs.tar.gz .'
```

Then `docker cp`, assert `./` first, `fakefsify`, and re-run the Round 25 test. Expected: `foot` opens
a PTY and the compositor log finally shows a **mapped toplevel** (`map toplevel … foot`), which is the
first window on the Arch root — after which Steam is the remaining step.

## Round 26: the `/dev` fix works — `foot` gets past the PTY and stops in its own fd manager

`evidence/21-empty-dev.txt`, on `arch-steam3` (image rebuilt with `/dev`, `/proc`, `/sys` emptied
before tarring):

```
chdir: OK
dev: console full mqueue null ptmx pts random shm tty urandom zero     <- kernel created them
pacman: acl 2.4.0-1
foot:  err: fdm.c:228: no such FD: 6
toplevels: 0
```

* `/dev` is now correct: shipping it **empty** lets the kernel populate it (`null`, `zero`, `ptmx`,
  `pts`, `shm`, …), exactly as `app/AppDelegate.m` does on the device. The earlier
  `failed to open PTY` is **gone** — that fix is confirmed.
* `chdir` and `pacman` still work, so the two earlier wins hold in this image.
* `foot` now fails *later*, inside its own fd bookkeeping (`fdm.c:228: no such FD: 6`), and no
  toplevel is mapped yet.

Next, in this order — the last error smells like an artifact of how I launched it, so rule that out
first:

1. Run `foot` **without** `timeout` and **without** shell redirection (both manipulate fds), e.g.
   `(WAYLAND_DISPLAY=wayland-0 foot -e /bin/sh -c 'echo FOOT_RAN' </dev/tty >/dev/tty 2>&1 &)` then
   check the compositor log for `map toplevel`.
2. If it still fails, try a *different* client to separate "foot is unhappy" from "the session is
   unhappy": install `wayland-utils` (`wayland-info` prints the globals without needing a PTY) and
   `weston` (its `weston-simple-shm` maps a real toplevel), and run those.
3. Only when a toplevel maps does Steam become the next question — and Steam needs Xvnc + Mesa in
   this image first (`tigervnc`, `mesa`, `vulkan-virtio`), which is the Round 20/21 recipe with a
   few more packages.

Recipe note for every future image: `rm -rf /dev/* /proc/* /sys/*` before `cd / && tar -czf … .`
(`rm` will print thousands of "Read-only file system" lines for `/sys` and `/proc` — harmless; send
them to a file inside the container, never to this session's output).

## Round 27: three bugs peeled in sequence — the survivor is the controlling-TTY ioctl

`evidence/22-foot-retry.txt`, same `arch-steam3` image, `foot` launched **without** `timeout` or pipes
(so the harness is ruled out; `foot --version` reports `1.28.0 … +pgo +ime …` fine):

```
foot alive: no
foot log:  err: slave.c:554: /bin/sh: failed to execute: Inappropriate ioctl for device
           err: fdm.c:228: no such FD: 6
toplevels: 0
```

The error changed again, which is how the three separate defects have been coming apart:

| Round | Error | Cause | State |
| --- | --- | --- | --- |
| 21/22 | `failed to change working directory to /: Not a directory` | archive lacked a `./` entry → fakefs `/` not a directory | **fixed** (Round 24) |
| 25/26 | `failed to open PTY: No such file or directory` | image shipped a populated `/dev`, shadowing the kernel's nodes | **fixed** (Round 26) |
| 27 | `failed to execute: Inappropriate ioctl for device` (`ENOTTY`) | the **controlling-terminal ioctl** on the pts device, done by `foot`'s slave just before `execve` | **open** |

`foot`'s slave opens the PTY slave, calls the "become controlling terminal" ioctl, dups it onto 0/1/2
and then execs; `ENOTTY` there means that ioctl is not handled for the guest's `/dev/pts/N`. Note the
app's *own* terminal works, so iSH clearly supports PTYs for its own path — the question is whether
the ioctl is implemented for an arbitrary process using a pty it opened itself.

**Next investigation, exactly:** `grep -rn 'TIOCSCTTY\|TIOCGPTN\|TIOCSPTLCK\|TCGETS\|TCSETS' fs/ kernel/`
— `fs/tty.c`, `fs/pty.c`, `fs/ptmx.c` (whichever exist) — and compare with how the app's
`app/LinuxPTY.c` / `PtyConsole` path sets up its terminal. If `TIOCSCTTY` is simply missing, adding it
is the same kind of small kernel addition this project has made repeatedly (`close_range`,
`faccessat2`), and it is the last thing standing between the Arch root and its first mapped window.
Steam still needs `tigervnc` + `mesa` + `vulkan-virtio` in the image, after that.

### Round 27, confirmed: `TIOCSCTTY` does not exist in this tree

```
$ grep -rn 'TIOCSCTTY' . --include='*.c' --include='*.h'      # no hits
$ grep -rn 'TIOCSPTLCK\|TIOCGPTN' fs/                          # pty.c:85, pty.c:88
$ sed -n '78,95p' fs/pty.c                                    # the pty ioctl dispatch switch
```

So the guest's pty layer handles *unlock* (`TIOCSPTLCK`) and *get number* (`TIOCGPTN`) but has no
controlling-terminal ioctl at all — which is exactly the `ENOTTY` `foot`'s slave hits just before
`execve` (`slave.c:554`). `fs/tty.c`, `fs/tty-real.c` and `fs/real.c` (which does handle `TCGETS`/
`TCSETS` for host-backed ttys, `real.c:672-742`) are the places a fix would land; `fs/pty.c:85` is the
dispatch switch to extend.

This is the last identified blocker before a **mapped toplevel**: three defects, three fixes
(`./` in the archive, empty `/dev`, and now `TIOCSCTTY`), each confirmed by the error moving on to the
next one. Implementing `TIOCSCTTY` (and probably `TIOCNOTTY`) is a real kernel addition — the same
class as `close_range` and `faccessat2` in this project's history — and is the point at which the
work stops being packaging and becomes kernel work.

### Correction: `TIOCSCTTY` **is** implemented — my grep was truncated

The claim directly above is wrong, and the same command's later output shows why: I piped the first
grep through `head -12`, which cut the list before `fs/tty.h` and `fs/tty.c`:

```
fs/tty.h:68:  #define TIOCSCTTY_ 0x540e
fs/tty.c:665: case TCFLSH_: case TIOCSCTTY_:
app/LinuxInterop.c:122: err = vfs_ioctl(session->tty, TIOCSCTTY, 0);   // the app's own terminals use it
```

So the guest handles the controlling-terminal ioctl, and LinPad's own terminal path calls it
successfully. `foot`'s `ENOTTY` therefore comes from a **different** call in its slave setup — the
likely candidates are the **job-control ioctls** (`TIOCSPGRP`/`TIOCGPGRP`, which `tcsetpgrp()` uses)
or a termios call on the *new* pty, not from `TIOCSCTTY` being absent.

**Next step, precisely:** find which ioctl fails rather than guessing —
`grep -n 'case ' fs/tty.c | sed -n '1,80p'` to see the whole switch, check `TIOCSPGRP`/`TIOCGPGRP`
presence, and instrument `sys_ioctl`/`tty_ioctl` to print the command number and `tcgetattr`-level
details for the failing call. A three-line debug print will name it in one run.

Lesson for this file: never let a truncated grep become a conclusion. Two of my wrong diagnoses in this
investigation (statvfs, now this) came from reading a partial result as a complete one.

## Round 28: the `pollnval` noise is a known Darwin workaround — and it is the likely cause of `foot`'s error

Round 27's PTY matrix (`evidence/23-pty-matrix.txt`) settled two things:

* **The PTY layer works.** `script -qec …` under `setsid` allocated a pty, ran a command inside it and
  printed `SETSID_PTY_OK`, with `/dev/ptmx` present (`crw-rw-rw- 1 root root 5, 2`). So the failure is
  specific to how `foot` uses its fds, not to pty allocation.
* The run leaked repeated kernel diagnostics: `pollnval 0 flags 0 events 3 revents 32`. `revents 32`
  is `POLLNVAL`.

That print is `fs/real.c:319`, inside an explicit macOS workaround for **host-backed** ("adhoc") fds:

```c
if (p.revents & POLLNVAL) {
    printk("pollnval %d flags %d events %d revents %d\n", fd->real_fd, flags, p.events, p.revents);
    // Seriously, fuck Darwin. I just want to poll on POLLIN|POLLOUT|POLLPRI.
    // … So ask for each separately and ignore a POLLNVAL.
    for each of POLLIN/POLLOUT/POLLPRI: poll(&p, 1, 0) and ignore POLLNVAL …
```

So the noise is *intentional*, but the retry loop has a hole that matches the symptom exactly: when
Darwin reports `POLLNVAL` with no meaningful bits, the per-bit retries all return 0, and **`p.revents`
can still carry `POLLNVAL` out to the caller**. A caller that treats `POLLNVAL` as "this fd does not
exist" — which is precisely what `foot`'s fd manager does (`fdm.c:228: no such FD: 6`) — then gives up.

**Proposed fix** (small, inside existing code, no new subsystem): in that branch, once the per-bit
retry has answered the meaningful bits, clear `POLLNVAL` from `p.revents` for fds that are known to be
valid (the loop already knows `fd->real_fd` and `is_adhoc_fd(fd)`), and only leave `POLLNVAL` in place
when the fd is genuinely gone (e.g. `fcntl(fd->real_fd, F_GETFD) < 0 && errno == EBADF`).
Then rebuild the CLI (`ninja -C build-arm64-release`) and re-run the Round 27 test: `foot` should reach
its event loop and the compositor log should finally show `map toplevel … foot`.

Caveat worth carrying: this is on the **CLI/Darwin** path. The app runs the same code on the same host
OS, so the fix is likely necessary there too — but the iPad may differ in which fds are host-backed,
so the device remains the final word.

## Round 29: POLLNVAL patch changed nothing; `TIOCSCTTY` is implemented but not for the *pty slave*

**What I did**: patched the Darwin `POLLNVAL` branch in `fs/real.c` (clear it when
`fcntl(fd->real_fd, F_GETFD) >= 0`), rebuilt the CLI (`ninja -C build-arm64-release ish`, 53/53 ok),
and re-ran the Round 27 test on the same `arch-steam3` root (`evidence/24-pollnval-fix.txt`):

```
foot alive: no
foot log:  err: slave.c:554: /bin/sh: failed to execute: Inappropriate ioctl for device
           err: fdm.c:228: no such FD: 6
toplevels: 0
```

**Identical output.** So the `POLLNVAL` print is downstream noise, not the cause — the primary error is
the ioctl, and my Round 28 hypothesis was wrong for the second time on this symptom. The patch is
still in the tree (it is defensible: the diagnostic was real and the clear only fires for live fds),
but it is **unproven and changed nothing**, so the next session may keep or revert it freely.

**What the code actually says** (this is the useful part):

```
fs/tty.c:654  tty_ioctl_size()   case TIOCSCTTY_ -> 0        // recognised
fs/tty.c:773  tty_ioctl()        case TIOCSCTTY_: …          // line 801 -> tiocsctty()
fs/tty.c:668  tiocsctty(tty, force)                          // full implementation: session,
                                                             // controlling-tty and superuser rules
fs/pty.c      pty_master_ioctl() … and no pty_slave_ioctl() in this file
```

`TIOCSCTTY` is implemented properly — for the fd type that uses `tty_ioctl` (see
`.fd.ioctl = tty_ioctl` at `fs/tty.c:882`). The suspicion is therefore precise: **the pty *slave*
(`/dev/pts/N`) has a different ioctl path**, and `TIOCSCTTY` on it returns `ENOTTY` — which is exactly
what `foot`'s slave hits on a pty **it opened itself**, before `execve`. LinPad's own terminal may
never hit this because `app/LinuxInterop.c:122` calls the ioctl on the session tty, not on a
freshly-opened pts.

**Next step**: in `fs/pty.c`, find the slave fd's ops (the grep for `pty_slave` / `.fd.ioctl` is in the
transcript above) and route `TIOCSCTTY_` to `tiocsctty()` there (exporting it from `fs/tty.c` if it is
`static`). That is a handful of lines and is the last barrier to a mapped toplevel — after which the
image needs `tigervnc`, `mesa` and `vulkan-virtio` before Steam can be tried.

### Round 29, confirmed at source: the pty **slave** has no `.ioctl` at all

```
fs/pty.c:113  const struct tty_driver_ops pty_master_ops = { … .ioctl = pty_master_ioctl, … };
fs/pty.c:10   extern struct tty_driver pty_slave;
fs/pty.c:15   pty_slave_init_inode / :64 pty_slave_open / :72 pty_slave_close / :54 pty_slave_cleanup
              — and no slave ioctl anywhere in the file
```

There is a `pty_master_ops` with an `.ioctl`, and the **slave driver has no ioctl handler** (its ops
struct has no `.ioctl`). A program that opens `/dev/pts/N` itself and calls `TIOCSCTTY` — which is
precisely what `foot` does before `execve` — therefore gets `ENOTTY` from the generic path, while
LinPad's own terminal never notices because `app/LinuxInterop.c:122` issues the ioctl against the
session tty (a different driver, `fs/tty.c:882 .fd.ioctl = tty_ioctl`).

**The patch is now almost mechanical**: give the slave driver ops an `.ioctl` that forwards to the same
handling the master/tty path uses — i.e. route `TIOCSCTTY_` to `tiocsctty()` (`fs/tty.c:668`, exported
from `static` if needed) and, for the rest, fall through to the generic tty ioctl behaviour. Then
rebuild the CLI, re-run the Round 27/29 test, and expect `map toplevel … foot` in the compositor log.

After that, the remaining work is no longer debugging: add `tigervnc`, `mesa`, `vulkan-virtio` to the
image (Round 20/21 recipe + empty-`/dev` tar step), migrate the Steam client host-side, and take the
head-side Big Picture screenshot.

## Round 30: the patch is right but I could not land it — exact text for the next attempt

I applied the pty-slave ioctl, could not get it to compile, and after two botched edit/revert cycles
**restored `fs/pty.c` and `fs/tty.c` to HEAD** to leave a building tree. The two earlier fixes
(`tools/fakefs.c` hardlink `EEXIST`, `fs/real.c` POLLNVAL clear) are untouched and still present.

What I learned, so the next attempt is one shot:

1. `UNUSED()` in this tree is a **parameter-list annotation**, not a body macro
   (`static int pty_return_eio(struct tty *UNUSED(tty))` in `fs/pty.c`). Writing `UNUSED(arg);` in the
   body fails with `use of undeclared identifier 'UNUSED_arg'`.
2. My revert left the old definition behind, so the second application produced a duplicate — hence
   `1 error generated` with two identical-looking definitions in the file. **Check
   `grep -c 'static int pty_slave_ioctl' fs/pty.c` is exactly 1 and `grep -c '\.ioctl = pty_slave_ioctl'`
   is exactly 1 before building.**

The patch, verbatim, in `fs/tty.c` (one word):

```diff
-static int tiocsctty(struct tty *tty, int force) {
+int tiocsctty(struct tty *tty, int force) {
```

and in `fs/pty.c`, immediately before `const struct tty_driver_ops pty_slave_ops = {`:

```c
int tiocsctty(struct tty *tty, int force);

static int pty_slave_ioctl(struct tty *tty, int cmd, void *UNUSED(arg)) {
    if (cmd == TIOCSCTTY_)
        return tiocsctty(tty, 0);
    return _ENOTTY;
}
```

plus `.ioctl = pty_slave_ioctl,` in `pty_slave_ops` (after `.close = pty_slave_close,`).

Then: `ninja -C build-arm64-release ish`, and re-run the Round 27/29 test on the existing
`arch-steam3` root (no image rebuild needed) — expecting `map toplevel … foot` in the compositor log.

## Round 31: the patch landed, compiles — and changed nothing. Stop guessing; instrument.

`evidence/26-slave-ioctl-landed.txt`, with `pty_slave_ioctl` compiled into the CLI (build clean,
`[4/4] Linking target ish`):

```
foot alive: no
foot log:  err: slave.c:554: /bin/sh: failed to execute: Inappropriate ioctl for device
           err: fdm.c:228: no such FD: 6
toplevels: 0
```

Byte-identical to before the patch. So my sixth hypothesis (pty slave has no ioctl handler → that is the
`ENOTTY`) is **also wrong, or at least not the failing call**. The patch is left in the tree: it is
correct in itself (a slave driver with no ioctl handler is a real gap, and `TIOCSCTTY` on a pts is a
legitimate thing for a program to do), it compiles, and it is inert for this symptom. Anyone may revert
it freely; nothing depends on it.

**Six hypotheses, six misses — the method is the problem, not the ideas.** Every remaining guess about
*which* call returns `ENOTTY` costs a round and has a ~1-in-6 chance. The next step must be
measurement:

1. Add a temporary diagnostic in the tty ioctl paths — `fs/tty.c`'s `tty_ioctl` (`fs/tty.c:773`) and
   the driver dispatch that calls a driver's `.ioctl` — printing the **command number** (as hex) and
   the caller's `comm`, e.g. `printk("ttyioctl %x cmd=%x comm=%s\n", fd, cmd, current->comm);`
   The failing call names itself in one run; foot's slave path is only a handful of ioctls.
2. While in there, check the candidate commands this tree does **not** have (grep in the transcript
   above): `TIOCGPTPEER` (0x5441) is the modern way to open the pts from the master and returns
   `ENOTTY` where unimplemented; `TIOCGPTLCK`, `TIOCSIG`, `TCXONC`, `TIOCSTI` are the other gaps.
3. Only then write the fix, rebuild, and re-run this test on `arch-steam3` (no image rebuild needed).

Everything else stands: `arch-steam3` boots with a real `/`, a populated-then-emptied `/dev`, working
`pacman`, a running compositor and a PTY layer that `script` uses happily — the first mapped toplevel
is one measured ioctl away, not another guess.

## Round 32: the ioctl is measured — it is `TCGETS2` (0x802c542a), and foot is right to fail

Temporary instrumentation in `kernel/fs.c:752` (`sys_ioctl`), printing only for `comm` containing
"foot" (one line, marked as a diagnostic):

```c
if (strstr(current->comm, "foot")) printk("FOOTIOCTL fd=%d cmd=%x\n", (int) f, (int) cmd);
```

One run (`evidence/27-ioctl-trace.txt`) gives the whole conversation between `foot`'s slave and fd 6:

| cmd | decoded | handled by this tree? |
| --- | --- | --- |
| `0x80045430` (×2) | `TIOCGPTN` (get pty number) | yes — `fs/pty.c:88` |
| `0x40045431` | `TIOCSPTLCK` (unlock pty) | yes — `fs/pty.c:85` |
| `0x5414` | `TIOCSWINSZ` | yes — `fs/tty.c:659` |
| `0x540e` | `TIOCSCTTY` | yes — and now for the slave too (Round 31 patch) |
| **`0x802c542a`** | **`TCGETS2`** — `_IOR('T', 0x2a, struct termios2)`, 44 bytes | **NO — this is the `ENOTTY`** |

`TCGETS2` is the Linux `termios2` getter; glibc's `tcgetattr` prefers it because `termios2` carries
`c_ispeed`/`c_ospeed` alongside the classic `struct termios`. The tree handles `TCGETS_` (`0x5401`) and
`TCSETS_`/`TCSETSW_`/`TCSETSF_`, but not the `*2` family, so the call returns `ENOTTY` and `foot`'s slave
dies before `execve` — exactly the error it reported all along. Six hypotheses failed because the
answer was a *missing ioctl number*, which only a trace could reveal.

**The fix, now precisely located** (`fs/tty.c`):

1. In `tty_ioctl_size()` (`fs/tty.c:654`) add `TCGETS2_` (0x802c542a) and `TCSETS2_`/`TCSETSW2_`/
   `TCSETSF2_` (0x402c542b/c/d) with `sizeof(struct termios2_)` — 44 bytes — so the argument is copied.
2. In `tty_ioctl()` (`fs/tty.c:773`) handle them the way the existing `TCGETS_`/`TCSETS_` cases do,
   filling/consuming a `termios2` (the classic termios plus the two speed fields).
3. Add the constants to `fs/tty.h` next to the existing `*_` definitions.
4. Rebuild, re-run this test on `arch-steam3`, expect `map toplevel … foot` in the compositor log.

Also: the `FOOTIOCTL` print in `kernel/fs.c` is a **temporary diagnostic** — remove it once the ioctl
family is implemented (it is filtered to `comm` containing "foot", so it is quiet, but it should not
outlive the investigation).

## Round 33: the `termios2` fix works — `foot` now fails on fonts, not on the tty

Implemented the measured fix: `TCGETS2_`/`TCSETS2_`/`TCSETSW2_`/`TCSETSF2_` added to `fs/tty.h`, mapped
onto the existing termios cases in `fs/tty.c` (both the size function and the implementation switch —
the first attempt only reached the size function, which the occurrence counts caught:
`grep -c TCGETS2_ fs/tty.c` must be **2**, not 1).

The trace and the error both moved (`evidence/28-termios2.txt`):

```
FOOTIOCTL fd=6 cmd=802c542a      <- TCGETS2   (was ENOTTY; now answered)
FOOTIOCTL fd=6 cmd=402c542b      <- TCSET2    (answered too)
foot log:  err: terminal.c:1138: failed to load primary fonts
```

Before: `slave.c:554: /bin/sh: failed to execute: Inappropriate ioctl for device`.
After: `terminal.c:1138: failed to load primary fonts`.

So `foot` now opens its pty, sets termios, forks the shell and **reaches its renderer** — the last thing
missing is a font. That is a packaging gap, not kernel work: the image was built with only
`wayland libxkbcommon zlib libx11 foot` and ships no fonts at all.

**Next step (small and mechanical):** add `fontconfig` and a monospace font
(`pacman -S --needed --disable-sandbox fontconfig ttf-dejavu`) to the Round 20/21 image recipe — together
with the empty-`/dev` tar step and the GNU-tar root entry — then re-run this test and expect
`map toplevel … foot` in the compositor log. The persisting `fdm.c:228: no such FD: 6` is most likely a
cascade of foot shutting down after the fatal font error; re-check whether it survives once fonts exist.

Also remember to remove the temporary `FOOTIOCTL` print in `kernel/fs.c:753` when the client test passes.

### The scoreboard after 13 rounds of Phase 1 debugging

| # | Defect | Where | State |
| --- | --- | --- | --- |
| 1 | archive had no `./` root entry → `/` not a directory | build step | fixed |
| 2 | image shipped a populated `/dev`, shadowing device nodes | build step | fixed |
| 3 | `chdir("/")` (same cause as 1) | — | fixed |
| 4 | `pacman`/libalpm `ALPM_ERR_NOT_A_DIR` (same cause as 1) | — | fixed |
| 5 | pty slave had no ioctl handler | `fs/pty.c` | fixed (inert for this symptom) |
| 6 | **`TCGETS2`/`TCSETS2*` missing** | `fs/tty.c` `fs/tty.h` | **fixed — this was the blocker** |
| 7 | no fonts in the image | build step | **open, one line** |

## Round 35: the SHM sealing gap is a missing `fcntl` command, and the fix is bounded

Located it in one grep:

```
kernel/memfd.c:16   #define MFD_ALLOW_SEALING_ 2
kernel/memfd.c:66   fd_t sys_memfd_create(addr_t name_addr, uint_t flags) { … }
```

`memfd_create` exists and understands `MFD_ALLOW_SEALING`, but **nothing in the tree implements
`F_ADD_SEALS`/`F_GET_SEALS`** (only the create-side flag appears). So `fcntl(fd, F_ADD_SEALS, …)`
falls through to an unknown-command path and returns `EINVAL` — exactly `foot`'s
`shm.c:468: failed to seal SHM backing memory file: Invalid argument`.

**What to add** (small, and the semantics can be honest rather than complete):

* Constants: `F_ADD_SEALS 1033 (0x409)`, `F_GET_SEALS 1034 (0x40A)`; seal bits `F_SEAL_SEAL 1`,
  `F_SEAL_SHRINK 2`, `F_SEAL_GROW 4`, `F_SEAL_WRITE 8`, `F_SEAL_FUTURE_WRITE 16`.
* Where: the `fcntl` implementation (`grep -rn 'sys_fcntl' kernel/fs.c fs/*.c` — the transcript above
  names the file) next to the other fd-level commands.
* Behaviour: keep a small `seals` bitmask on the memfd/shm file (`kernel/memfd.c` already has the
  object to hang it on) and
  - `F_ADD_SEALS`: reject only if `F_SEAL_SEAL` is already set (then `EPERM`), otherwise OR the bits in
    and return 0;
  - `F_GET_SEALS`: return the mask.
  Because the fakefs never shrinks or grows a client's SHM buffer behind its back, actually *enforcing*
  the seals is optional; what matters to clients like `foot` (and to any Wayland client that uses
  `wl_shm` pools) is that the call succeeds and reads back plausibly.

This is the same shape as the `TCGETS2` fix: a client-visible command the tree never implemented, found
by measurement, fixable in a handful of lines. It is worth doing **before** Steam's UI runs, because
Chromium/CEF's shared-memory paths are far more demanding about sealing than `foot` is.

Note: the `FOOTIOCTL` diagnostic in `kernel/fs.c:753` is still in place and still to be removed once the
client tests are done.

## Round 37: Steam installs on Arch with **zero** missing libraries — the premise is vindicated

In a `menci/archlinuxarm` container, with pacman's sandbox disabled:

```
$ pacman -S --needed tigervnc mesa vulkan-virtio lsof ca-certificates dbus xkeyboard-config fontconfig ttf-dejavu
$ tar -xzf steam-client.tar.gz -C /root/.local/share/Steam

steam tree:      324M
steam binary:    10,174,384 bytes   ($B/steamrtarm64/steam)
missing libs:    0                  ($ ldd $B/steamrtarm64/steam | grep -c "not found")
Xvnc:            /usr/sbin/Xvnc
mesa DRI:        swrast … (llvmpipe path for the X route)
vulkan icd:      /usr/share/vulkan/icd.d/virtio_icd.json
```

`ldd` resolving **completely** is the answer to the question this whole distro plan was built on. On
Alpine, that same binary needed the entire `/opt/steam-host` Debian glibc island, an explicit
`--library-path`, `GLIBC_TUNABLES=glibc.pthread.stack_cache_size=0`, and `ld-linux --list` checks in the
installers. On Arch it links against the system's own glibc and libraries, and Xvnc, Mesa and the Venus
Vulkan ICD all come from the distro. That is "native Steam, no transplants", measured rather than hoped.

**Important**: that container was thrown away after the check (it was a probe, not a build), so **no ready
image exists yet** — the install above is deterministic and takes a few minutes to repeat.

### Next: the head-side Big Picture test (the last unknown before the device)

```sh
D="docker --context colima-linpad-fex"; G=work/steam-distro/guest-tools/arch
id=$($D create menci/archlinuxarm:latest sh -c 'while :; do sleep 3600; done'); $D start "$id"
$D exec "$id" sh -c "sed -i 's/^DownloadUser/#DownloadUser/' /etc/pacman.conf; sed -i '/^\[options\]/a DisableSandbox' /etc/pacman.conf"
$D exec "$id" sh -c 'pacman -Sy --noconfirm --disable-sandbox && pacman -S --noconfirm --needed --disable-sandbox \
    tigervnc mesa vulkan-virtio lsof ca-certificates dbus xkeyboard-config fontconfig ttf-dejavu xorg-xwd'
#  the session layer (Round 20/21) + the Steam client + our init, then:
$D cp work/steam/serve/steam-client.tar.gz "$id:/tmp/steam.tgz"
$D exec "$id" sh -c 'mkdir -p /root/.local/share/Steam && tar -xzf /tmp/steam.tgz -C /root/.local/share/Steam'
$D exec "$id" sh -c 'rm -rf /dev/* /proc/* /sys/* 2>/dev/null; mkdir -p /dev /proc /sys; cd / && tar -czf /rootfs.tar.gz . 2>/tmp/t.err'
$D cp "$id:/rootfs.tar.gz" work/steam-distro/arch-steam-full.tar.gz; $D rm -f "$id"
#  import, then run the route and capture a frame:
fakefsify arch-steam-full.tar.gz arch-steam-full
#  inside the guest: Xvnc :55 -geometry 1024x700 -depth 24 -SecurityTypes None -localhost -rfbport 5955 -ac &
#                    DISPLAY=:55 steam -no-cef-sandbox -cef-disable-gpu -gamepadui &
#                    xwd -root -display :55 -out /tmp/bp.xwd   (then convert/inspect)
```

Expected: either Big Picture renders (screenshot evidence) or the client refuses `-gamepadui` and the
classic UI appears — the one question that has been open since the original plan. After that: the
app-side per-root work and the iPad run, and remove the `FOOTIOCTL` print from `kernel/fs.c:753`.

## Round 38: the whole stack runs head-side — Steam stops on the missing `~/.steam/steam` data link

Built `arch-steam-full` (container tar **1.4 G**, `./` first, imported with the fixed `fakefsify`) with
the session layer, `tigervnc`, `mesa`, `vulkan-virtio`, `xorg-xwd`, `xorg-xwininfo`, fonts and the Steam
client (324 M), then ran the route (`evidence/32-steam-headside.txt`):

* **X works**: `Xvnc :55` starts and the socket comes up (`x ready after N ticks`), and `xwd` captured a
  **2,870,379-byte** frame of the root window — the X server is live on the Arch root.
* **Steam started with the right arguments** and then aborted on a configuration assertion, not a
  library or syscall problem:

```
[2026-10-16 21:02:35] Startup - Steam Client launched with: '/root/.local/share/Steam/steamrtarm64/steam'
    '-no-cef-sandbox' '-cef-disable-gpu' '-gamepadui'
src/steamexe/main.cpp (1400) : Assertion Failed: Steam data link does not exist, client is misconfigured,
    cannot continue: /root/.steam/steam
```

`~/.steam/steam` (the "data link" to `~/.local/share/Steam`) does not exist in this image. The Alpine
device had it — its access logs show `/root/.steam/…` in use — because the transfer scripts created it.

**Fix, one line, before the tar step:**

```sh
$D exec "$id" sh -c 'mkdir -p /root/.steam && ln -sfn /root/.local/share/Steam /root/.steam/steam \
                     && ln -sfn /root/.local/share/Steam /root/.steam/root'
```

Then re-tar (`rm -rf /dev/* /proc/* /sys/*; cd / && tar -czf /rootfs.tar.gz .`), re-import, and re-run
the Round 38 block. Success looks like: `xwininfo -root -children` listing Steam's window(s) instead of
`0 children`, a `steam.log` with no assertion, and an `xwd` frame with actual UI in it.

Worth stating plainly: `-gamepadui` was **accepted as an argument** (no "unknown option"), which is
encouraging but is *not* proof that the Big Picture UI loads — the next run is what decides that, and it
is the last open unknown from the original plan.

## Round 39: Steam creates a window on the Arch root — and stops on the guest's network

Created the data link at runtime (the image still needs it baked in), started Xvnc, ran the client with
`-gamepadui` (`evidence/33-steam-gamepadui.txt`):

```
steam alive: yes
--- windows:
     1 child:
     0x200003 "Steam - Fatal Error": ()  854x129+85+285  +85+285
--- steam log:
Error: Steam needs to be online to update. Please confirm your network connection and try again.
Fatal error: Steam needs to be online to update.
```

Two genuine advances: the `~/.steam/steam` fix cleared the misconfiguration assertion, and Steam got far
enough to **create an X window** — which `xwininfo` lists — so the compositor/X/window path is proven with
the real client, not just `foot`. The failure is now purely **guest networking**: Steam's bootstrap wants
to update and cannot reach the network inside the CLI session. That is expected here (the app calls
`configureDns` and sets up the socket path at boot; the Alpine device had working networking, which is how
Steam signed in and downloaded Hollow Knight in build 4441), so this is likely a CLI-environment
limitation rather than an Arch problem.

**To separate the two, next session, in order:**

1. Test the guest's own networking in the CLI: `curl -sS -o /dev/null -w '%{http_code}\n' https://store.steampowered.com`
   (also try `ping`, and `getent hosts`), and check `/etc/resolv.conf` — if DNS or sockets fail there, it
   is the CLI environment, not Steam.
2. If networking works but Steam still insists on updating, skip the bootstrap update and go straight to
   the UI: `echo 'BootStrapperInhibitAll=enable' > $B/steam.cfg` (and/or run with `-noreactlogin`), or use
   Steam's offline mode. The goal of this test is the *UI mode*, not an account login.
3. If the CLI cannot provide networking at all, the head-side test has reached its limit and the
   `-gamepadui` answer must come from the **iPad**, where networking is known-good — i.e. the device run
   becomes the next step rather than a later one.

Also still pending, and cheap to fold into the next image build: bake in
`/root/.steam/{steam,root}` symlinks, and remove the temporary `FOOTIOCTL` print in `kernel/fs.c:753`.

## Round 40: bootstrap update suppressed — now `steamui.so` needs `libXrandr`

`evidence/34-steam-noupdate.txt`, with `steam.cfg` = `BootStrapperInhibitAll=enable`:

```
[2026-10-16 21:07:22] Suppressing Steam update
dlmopen /root/.local/share/Steam/steamrtarm64/steamui.so failed: libXrandr.so.2: cannot open shared object file
Failed to load steamui.so - dlerror(): (null)
xwd bytes: 2870379
```

Two things settled:

* **The update bypass works.** `Suppressing Steam update` means the earlier "needs to be online to update"
  fatal error is out of the way — and since Steam reached that decision, the guest had enough network to
  load its update configuration. The remaining failures are therefore not about the network at all.
* **The next blocker is a missing library, and `steamui.so` is the library that contains Big Picture.** The
  client binary's own `ldd` was clean, but `steamui.so` is `dlmopen`'d *later* and has its own needs:
  `libXrandr.so.2` is not in the image.

**The recipe from here is deterministic** — close the dependency graph the same way the Steam port was
built on Alpine, but against pacman:

```sh
B=/root/.local/share/Steam
LP=$B/steamrtarm64:$B/steamrtarm64/libs
# in the container:
LD_LIBRARY_PATH=$LP ldd $B/steamrtarm64/steamui.so        | grep 'not found'
LD_LIBRARY_PATH=$LP ldd $B/steamrtarm64/steamwebhelper    | grep 'not found'
LD_LIBRARY_PATH=$LP ldd $B/steamrtarm64/libcef.so         | grep 'not found'
# install whatever appears (libxrandr first; expect the rest of the set), repeat until zero:
pacman -S --needed --disable-sandbox libxrandr libxinerama libxcursor libxi libxcomposite libxdamage \
    libxfixes libxtst libxss libxrender libxext libglvnd gdk-pixbuf2 gtk3 nss libcups alsa-lib libpulse
```

Then re-tar (`rm -rf /dev/* /proc/* /sys/*; cd / && tar -czf /rootfs.tar.gz .`), re-import, and re-run the
Round 39/40 block. Success is `xwininfo -root -children` listing Steam's window with real geometry and a
`steam.log` with no `dlmopen` failure.

Also still to fold in: bake `/root/.steam/{steam,root}` into the image (currently created at runtime),
bake `steam.cfg`, and remove the `FOOTIOCTL` print from `kernel/fs.c:753`.

## Round 41: in-guest `pacman` cannot fetch — dependency closure has to happen in the container

Tried the cheap path: install the missing libraries from inside the running guest (`pacman -Sy` +
`pacman -S --disable-sandbox …`) so the 1.4 GB image would not need rebuilding. It did not work — the
`ldd` of `steamui.so` still reports **8 missing libraries** afterwards, and Steam fails exactly as before
(`dlmopen … steamui.so failed: libXrandr.so.2`), so nothing was installed. The `sync rc=` / `install rc=`
lines are in `evidence/35-steam-inguest-deps.txt`.

That is consistent with Round 39's observation and settles it: in the Mac-hosted CLI environment the guest
has **no usable package-manager networking** (the app provides that path at boot; the CLI does not). Two
consequences, both worth knowing:

* **Dependency closure happens in the container**, as the Round 40 recipe says — `pacman` there has network
  and works. So the next round is an image rebuild with the extra packages, not a quick in-guest fix.
* **On the iPad this constraint disappears** — the device has working networking (that is how Steam signed
  in and downloaded Hollow Knight in build 4441) — so in-guest package management is a *device* capability
  and cannot be exercised from this harness either way.

Also worth noting from the run: Steam spawned a `-child-update-ui` subprocess before failing, and still
printed `Suppressing Steam update` — the update bypass holds.

## Round 42: closure advancing (libXrandr fixed → GTK2 next) — but stop adding one library per round

Rebuilt the image with the guessed X/GTK/NSS set and re-ran (`evidence/36-bigpicture.txt`):

```
steamui missing: 7                       <- was 8
dlmopen … steamui.so failed: libgtk-x11-2.0.so.0: cannot open shared object file
[21:20:02] Suppressing Steam update      <- the bypass still holds
```

`libXrandr` is resolved; the first missing library is now **GTK2** (`libgtk-x11-2.0.so.0`), which Valve's
`steamui` links against. The image also got *smaller* (766 M tar) — the earlier 1.4 G was mostly Mesa's
full DRI set.

**Change the method: enumerate the whole closure, then install once.** Guessing a package set has now cost
two rounds for one library each. In the container, right after extracting the client:

```sh
B=/root/.local/share/Steam; LP=$B/steamrtarm64:$B/steamrtarm64/libs
for so in $B/steamrtarm64/steamui.so $B/steamrtarm64/steamwebhelper $B/steamrtarm64/libcef.so; do
    echo "== $so"; LD_LIBRARY_PATH=$LP ldd "$so" | grep 'not found'
done
```

Then map every name to its Arch package and install them together (`pacman -S --needed --disable-sandbox …`),
re-run the `ldd` loop until it prints nothing, and only then tar/import/run. Expected candidates beyond what
is already installed: **`gtk2`**, **`libudev` (package `systemd-libs` — installable without running systemd)**,
`libnm` (NetworkManager), `libappindicator-gtk2` / `libdbusmenu-gtk2`, `libxcb-keysyms`, `libxtst`,
`libpciaccess`, `libxshmfence`. Also worth checking `steamwebhelper` and `libcef.so` at the same time — they
have their own needs, and CEF is what actually renders Big Picture.

This is the last mechanical step before the `-gamepadui` answer: nothing here is a kernel or glibc question
any more, only package names.

## Round 43: one bad package name aborts pacman atomically — so nothing was installed

`evidence/37-closure-run.txt`:

```
pacman exit=1                       <- the whole transaction failed
== steamui.so: 19 missing           (libSDL3.so.0, libGL.so.1, libX11.so.6, …)
== steamwebhelper: 12 missing       (libcef.so, libSDL3.so.0, libX11.so.6, …)
== libcef.so: 0 missing             <- CEF itself is self-contained here
tar=310M                            (base + Steam client, nothing else)
xwininfo: command not found         (because nothing was installed)
```

The long "missing" lists are an artefact, not a regression: **pacman is atomic, and one name in my list does
not exist in Arch Linux ARM's repos, so the entire transaction was rolled back** — no X libraries, no
xwininfo, hence 19 "missing" that the round-42 image already had. The suspects among what I added this time
are the GTK2-era indicator packages (`libappindicator-gtk2`, `libdbusmenu-gtk2`) and possibly `libxcb-keysyms`
/`libxshmfence`; verifying each name is now mandatory.

**Method fix (this is the round's lesson):**

```sh
for p in gtk2 systemd-libs sdl3 libglvnd libxrandr …; do
    pacman -S --noconfirm --needed --disable-sandbox "$p" >/dev/null 2>&1 \
        && echo "ok   $p" || echo "FAIL $p"
done
```

One package at a time, each failure named, and the *union* of what succeeded is the set to bake into the
image. Never a single `pacman -S a b c …` while the names are still guesses.

**And two genuinely useful facts from the enumeration, despite the failure:**

* **`libcef.so` needs nothing** — 0 missing even on the bare base image, so CEF ships what it needs; the
  failures are all in Valve's own `steamui.so`/`steamwebhelper` wrappers.
* `steamui.so`'s real dependency set starts with **`libSDL3.so.0`** (Arch: **`sdl3`**), then `libGL.so.1`
  (`libglvnd`), `libX11`, `libXext`, `libXrender`, `libXrandr`, `libXfixes`, `libXtst` — i.e. exactly what
  round 42's set was closing on, plus SDL3 and GTK2.

So the path is unchanged, only the *method*: install one-by-one with names printed, re-enumerate, publish
the successful set as the image's package list, then tar/import/run and read the `-gamepadui` answer.

## Round 44: the closure is now a three-name list (35 packages installed, 1 refused)

The one-at-a-time method worked exactly as intended:

```
sync=0
installed_ok=35
FAILED: gtk2
steamui.so missing=7      libgtk-x11-2.0.so.0, libavcodec.so.62, libavformat.so.62,
                          libswresample.so.6, libavutil.so.60, libswscale.so.9
steamwebhelper missing=2  libcef.so (ships in steamrtarm64 — a library-path matter, not a package),
                          libibus-1.0.so.5
libcef.so missing=0       CEF needs nothing from the distro
```

So the entire remaining dependency closure is **three names**:

| Missing | Arch package | Note |
| --- | --- | --- |
| `libav*` (5 libraries) | **`ffmpeg`** | one package |
| `libibus-1.0.so.5` | **`libibus`** | for `steamwebhelper` |
| `libgtk-x11-2.0.so.0` | **`gtk2`** — **refused to install** | see below |

**About `gtk2`**: the transaction failed while 35 other packages installed, so the name is wrong, the repo is
not enabled, or ALARM dropped it. Try in this order: `pacman -S extra/gtk2` (name the repo explicitly),
`pacman -Ss '^gtk2$'` to see what the mirror actually offers, and — only if ALARM genuinely has no GTK2 —
take the honest small fallback: extract `libgtk-x11-2.0.so.0` from a Debian/Ubuntu aarch64 package into
`/opt/gtk2/` and add that directory to the launcher's `LD_LIBRARY_PATH`. That is one library, far from the
`/opt/steam-host` island of the Alpine build, and it would be recorded as such.

**Next round is therefore short:** install `ffmpeg` + `libibus` (+ `gtk2` or its fallback), re-run the `ldd`
enumeration expecting **0 missing on all three libraries**, then tar (empty `/dev`, GNU tar, `./` first),
import, and run the Round 39/40 block — which finally answers whether `steam -gamepadui` reaches Big Picture
on the Arch root.

Also note for the image: the 35 successfully installed packages are effectively the image's package list
now; it is worth writing that exact list into `packages/base.txt` next round so the build stops being
exploratory.

## Round 45: ALARM has no GTK2, and its ffmpeg sonames are too new — the closure cannot close from ALARM alone

`evidence/38-bigpicture-attempt.txt`:

```
FAILED: gtk2
--- gtk2 detail:
error: target not found: gtk2
--- search:                       (pacman -Ss '^gtk2$' printed nothing)
steamui missing=7   libgtk-x11-2.0.so.0  libavcodec.so.62  libavformat.so.62
                    libswresample.so.6   libavutil.so.60   libswscale.so.9  libavfilter.so.11
steamwebhelper missing=1   (only libcef.so, which ships inside steamrtarm64 — a library-path
                            matter, not a package)
TAR=no
```

Two distinct facts, both about the distro rather than the client:

1. **Arch has dropped GTK2** — `target not found` and nothing in the search index. `steamui.so` links
   `libgtk-x11-2.0.so.0`, so the UI cannot load without it.
2. **`ffmpeg` installed, but the sonames do not match.** The client wants `libavcodec.so.62`,
   `libavformat.so.62`, `libswresample.so.6`, `libavutil.so.60`, `libswscale.so.9`, `libavfilter.so.11`;
   current Arch/ALARM ffmpeg provides different (newer) sonames. The ARM64 Steam client was built against an
   older FFmpeg, and Arch's rolling packages have moved past it — the exact failure mode a rolling distro is
   expected to have, and the reason the Alpine build ended up transplanting libraries in the first place.

**Two honest ways forward, in order of preference:**

1. **Take both from the Arch Linux ARM archive** (ALARM keeps older packages, e.g. the
   `tardis.tiny-vps.com/aarm` mirror): fetch the `gtk2` package and the ffmpeg version whose sonames match,
   install with `pacman -U`, pin them with `IgnorePkg` in `/etc/pacman.conf` so an upgrade cannot break the
   client, and verify by re-running the `ldd` enumeration. This keeps the image 100 % Arch.
2. **Bounded transplant** — extract `libgtk-x11-2.0.so.0` and those six FFmpeg libraries from the Debian
   trixie aarch64 packages this project already stages (`work/steam/serve/steam-gtk.tar.gz` and the
   steam-host closure) into `/opt/steam-compat/`, and add that one directory to the launcher's
   `LD_LIBRARY_PATH`. That is two library groups, not the whole `/opt/steam-host` island the Arch choice was
   meant to retire — and it must be recorded as a compatibility layer, not as "native".

Everything else is closed: 35+ packages install cleanly, CEF needs nothing, and `steamwebhelper`'s single
"missing" entry is the client's own library found through `LD_LIBRARY_PATH`.

**After either path**: re-enumerate (expect 0/0), tar with empty `/dev` and GNU tar (`./` first), import, run
the Round 39/40 block, and read the `-gamepadui` answer — the last unknown of the original plan.

## Round 46: GTK2 and libibus are already staged locally — only FFmpeg's sonames remain

Inspected the transplant tarballs this project already has in `work/steam/serve/` (no network needed):

```
steam-gtk.tar.gz         ./usr/lib/aarch64-linux-gnu/libgtk-x11-2.0.so.0  (+ .0.2400.33)   <- GTK2!
steam-cef-glibc.tar.gz   ./usr/lib/aarch64-linux-gnu/libibus-1.0.so.5    (+ .5.0.532)     <- libibus!
steam-glibc.tar.gz       (no libav*)
steam-mesa.tar.gz        (no libav*)
```

So two of the three open items are **available offline**, aarch64, glibc-built, and already used by the Alpine
Steam route: `libgtk-x11-2.0.so.0` for `steamui.so` and `libibus-1.0.so.5` for `steamwebhelper`. They only
need to be extracted into the image (e.g. `/opt/steam-compat/usr/lib/aarch64-linux-gnu/`) and added to the
launcher's `LD_LIBRARY_PATH`. That is a **two-library compatibility layer**, and it should be labelled as such
in the image's own documentation.

**The one real gap is FFmpeg's sonames** — nothing staged carries `libav*`, and the client wants
`libavcodec.so.62` / `libavformat.so.62` / `libswresample.so.6` / `libavutil.so.60` / `libswscale.so.9` /
`libavfilter.so.11`, which are **FFmpeg 8.x** sonames. Arch x86_64 ships FFmpeg 8; Arch Linux ARM lags, which
would explain why installing `ffmpeg` did not satisfy them. Try, in order:

1. **`pacman -Syu` first, then `pacman -S ffmpeg`** — my installs used `-Sy`, so the container may simply be
   pinned to an older ALARM snapshot. If ALARM's current ffmpeg is 8.x, this closes the gap outright.
2. If ALARM's ffmpeg is still 7.x: take the six libraries from a glibc aarch64 distro that ships FFmpeg 8
   (Debian trixie-backports, Ubuntu 25.10+, or Fedora aarch64) using the same `debfetch`-style fetch the
   project already has. **Do not** use Alpine's ffmpeg — musl-linked, unusable next to glibc.
3. Only if neither exists: check whether Valves's `steamrtarm64/libs` ships an FFmpeg (the earlier
   enumeration says no) or whether the UI loads without it (unlikely — `steamui.so` links it directly).

Then the run: extract GTK2 + libibus into `/opt/steam-compat/`, install/place FFmpeg, re-enumerate expecting
0/0, tar (empty `/dev`, GNU tar, `./` first), import, run the Round 39/40 block — the `-gamepadui` answer.

## Round 47: Arch is *ahead* of Valve's client, not behind it — the closure needs an older FFmpeg

`pacman -Syu` was a no-op (`packages=0`, "there is nothing to do"), so ALARM is fully current — and there is
the answer: current ALARM ships **`ffmpeg 2:9.0.2-2`**, whose sonames are *newer* than the client's:

| Client wants (`steamui.so`) | ALARM's ffmpeg 9 provides |
| --- | --- |
| `libavcodec.so.62` | `libavcodec.so.63` |
| `libavformat.so.62` | `libavformat.so.63` |
| `libavfilter.so.11` | `libavfilter.so.12` |
| `libavutil.so.60` | `libavutil.so.61` |
| `libswresample.so.6`, `libswscale.so.9` | (FFmpeg 9's own) |

So my earlier hypothesis (ALARM lagging) was backwards: **the rolling distro has moved past what Valve's ARM64
client was built against.** `gtk2` is confirmed absent from the repos even after the upgrade. This is the
honest cost of the Arch choice, and it is small: one older FFmpeg plus one dropped library.

**The cleanest route keeps everything Arch**: ALARM keeps historical packages (the `tardis.tiny-vps.com/aarm`
mirror), so fetch the **previous ALARM `ffmpeg`** (the one with the `so.62`/`so.60`/`so.11` family) and
**`gtk2`** from that archive, install both with `pacman -U`, and pin them so an upgrade cannot break the
client:

```
IgnorePkg = ffmpeg gtk2        # in /etc/pacman.conf [options]
```

`libibus` is already fine (it installed from ALARM), and `libgtk-x11-2.0.so.0` is additionally available
offline in `work/steam/serve/steam-gtk.tar.gz` if the archive has no gtk2 either.

**Fallbacks, in order**: Ubuntu 25.10 aarch64 ships FFmpeg 8 (the `so.62` family) — take those six libraries
into `/opt/steam-compat/` on `LD_LIBRARY_PATH`; or the same six from Debian trixie-backports if it has moved
to 8. Any of these is a documented, bounded compatibility layer (7 libraries), not the `/opt/steam-host`
island.

**After that**: re-enumerate (0/0), tar (empty `/dev`, GNU tar, `./` first), import, run the Round 39/40 block
— the `-gamepadui` answer.

## Round 48: sources for the compatibility layer are verified, not assumed

Endpoint checks (status codes):

```
http://tardis.tiny-vps.com/aarm/            301   <- the ALARM archive exists; my path just needed -L
https://mirror.archlinuxarm.org/aarch64/    000   <- not reachable from here
packages.ubuntu.com/questing/libavcodec62   200   <- Ubuntu 25.10 ships FFmpeg 8 (so.62)  ✅
packages.debian.org/sid/libavcodec62        200   <- Debian sid also has so.62            ✅
packages.debian.org/trixie/libavcodec61     200   <- Debian 13 is FFmpeg 7 (so.61, too old)
```

So the six FFmpeg-8 libraries the client wants have **two confirmed glibc/aarch64 sources** — Ubuntu 25.10
("questing") and Debian **sid** — and this project already has the fetch machinery for exactly that kind of
closure (`devtools/debfetch.mjs`, `debfetch-curl.mjs`, with per-file SHA-256 checks and a `.packages`
provenance record). Debian trixie is explicitly the wrong one (so.61).

**Next, in order:**

1. **Retry the ALARM archive with redirects** — `curl -sL http://tardis.tiny-vps.com/aarm/packages/f/ffmpeg/`
   (and `.../g/gtk2/`) — looking for an `ffmpeg-2:8.*` package (the so.62 family) and any `gtk2`. If both are
   there, `pacman -U` them and pin with `IgnorePkg = ffmpeg gtk2`: that keeps the image **100 % Arch**.
2. Otherwise build the documented 7-library compatibility layer: the six `libav*`/`libswresample`/`libswscale`
   libraries from Ubuntu 25.10 or Debian sid aarch64, plus `libgtk-x11-2.0.so.0` from the local
   `work/steam/serve/steam-gtk.tar.gz`, into `/opt/steam-compat/usr/lib/aarch64-linux-gnu/`, with the launcher
   adding that one directory to `LD_LIBRARY_PATH`. (`libibus-1.0.so.5` is already available locally too, and
   installs from ALARM.)
3. Re-enumerate `steamui.so` / `steamwebhelper` expecting **0/0**, then tar (empty `/dev`, GNU tar, `./`
   first), import, and run the Round 39/40 block for the `-gamepadui` answer.

The important framing for whoever picks this up: the compatibility layer is now a *known, sized* set of seven
libraries with verified sources — not an open-ended island. That is the honest shape of running Valve's ARM64
client on a distro that has moved past its build vintage.

## Round 49: there is no usable ALARM package archive — the seven-library layer is the decision

```
$ curl -sIL http://tardis.tiny-vps.com/aarm/packages/f/ffmpeg/
effective: http://tardis.tiny-vps.com/index.html 200      <- /aarm/ is gone
ffmpeg entries: (none)     8.x present: 0      gtk2 entries: (none)
```

The historical ALARM package archive this plan was counting on no longer exists at that path, so **the
Arch-pure route is closed**: there is no source for an older ALARM `ffmpeg` (the `so.62` family) and no
`gtk2` package to pin.

**Decision: build the documented compatibility layer, seven libraries.**

| Library group | Source | Status |
| --- | --- | --- |
| `libavcodec.so.62`, `libavformat.so.62`, `libavfilter.so.11`, `libavutil.so.60`, `libswresample.so.6`, `libswscale.so.9` | Ubuntu 25.10 (questing) or Debian sid, aarch64, via the project's `debfetch` tooling | endpoints verified (Round 48) |
| `libgtk-x11-2.0.so.0` | local `work/steam/serve/steam-gtk.tar.gz` (aarch64, glibc) | already on this Mac |
| `libibus-1.0.so.5` | local `steam-cef-glibc.tar.gz`, and ALARM installs `libibus` | available |

Place them in `/opt/steam-compat/usr/lib/aarch64-linux-gnu/` (mirroring the multiarch path the loader
already searches), have the launcher prepend that directory to `LD_LIBRARY_PATH`, and record the packages and
their SHA-256s in the image's provenance — this is a compatibility layer and must be named as one.

**Revised, honest framing of the Arch bet** (worth keeping in the plan's own words):

* What Arch delivers: the Steam client, its X server, Mesa, Vulkan, fonts and 35+ packages install from
  pacman and link against the distro's own glibc with **zero** missing libraries; no `/opt/steam-host`, no
  Debian glibc island, no `ld-linux --library-path`, no `GLIBC_TUNABLES` workarounds on the client path.
* What it costs: **seven libraries** must be transplanted, for two concrete reasons — Arch dropped GTK2, and
  Arch's FFmpeg has moved past the client's build vintage (9.x vs the client's 8.x sonames).
* That is a materially better position than Alpine's multi-hundred-megabyte island, but it is **not** "zero
  transplants", and the plan should say so rather than let the earlier framing stand.

**Next session's first task**, in order: fetch/build the seven libraries → place them → re-enumerate
`steamui.so` and `steamwebhelper` expecting 0/0 → tar (empty `/dev`, GNU tar, `./` first) → import → run the
Round 39/40 block → read the `-gamepadui` answer, which is the last unknown from the original plan.

## Round 51: assembly done, and the closure is *two* groups — GTK2 (incomplete extraction) and FFmpeg's codec set

Built the compat layer, installed 35 packages (`FAILED:` empty — everything installed), tarred (`TAR=788M`,
imported as `arch-bp4`), and ran. Result (`evidence/39-bigpicture-final.txt`):

```
compat: 14 files                                  <- 6 FFmpeg + symlinks, plus GTK2 (see below)
steamui.so missing=23    libgdk-x11-2.0.so.0, libzvbi.so.0, libcodec2.so.1.2, libjxl.so.0.11,
                         librav1e.so.0.8, libshine.so.3, libSvtAv1Enc.so.4, libtwolame.so.0,
                         libx265.so.216, libgme.so.0, libchromaprint.so.1, librabbitmq.so.4,
                         librist.so.4, libsrt-gnutls.so.1.5, liblilv-0.so.0, libmysofa.so.1,
                         libflite*.so.1 … (17 of the 23 are codecs)
steamwebhelper missing=1 libcef.so (expected — it ships in steamrtarm64)
runtime error: dlmopen … failed: libgdk-x11-2.0.so.0      <- the loader reports the first one only
```

**Two lessons, both mine to fix:**

1. **I extracted GTK2 incompletely.** `libgtk-x11-2.0.so.0` is not GTK2 alone: it needs its sibling
   `libgdk-x11-2.0.so.0` (and pango/atk/cairo-era companions). The fix is trivial and offline: extract **all**
   of `usr/lib/aarch64-linux-gnu/` from `work/steam/serve/steam-gtk.tar.gz` into
   `/opt/steam-compat/usr/lib/aarch64-linux-gnu/` — that tarball *is* the GTK closure the Alpine Steam route
   used, and because the compat directory is prepended to `LD_LIBRARY_PATH` its copies win over Arch's.
2. **Debian's `libavcodec62` carries its whole codec closure** — 17 further libraries (x265, jxl, rav1e,
   SvtAv1Enc, zvbi, codec2, shine, twolame, gme, chromaprint, rabbitmq, rist, srt, lilv, mysofa, flite…).
   That is not a defect of the plan, it is what a distro FFmpeg looks like; Arch's own ffmpeg has the same
   shape, which is why `pacman -S ffmpeg` would have pulled them too.

**The last mile, two options:**

* **Recommended (correct):** finish the closure from Debian sid with the same index → `curl` → `ar x` method
  used in Round 50 — the 17 names are listed above, and each resolves to a `pool/main/…` `.deb`. Then
  re-enumerate expecting **0/0** on `steamui.so`/`steamwebhelper`, and run.
* **Fast diagnostic (if the goal is only to see the UI):** `patchelf --remove-needed libav*.so.*
  libsw*.so.* steamui.so` to drop the video libraries the Big Picture *browsing* UI does not touch, then run.
  This is a diagnostic hack, not the shipped answer — video/broadcast paths would break.

Either way the remaining list is finite and named, the image builds and imports, and the loader's error has
moved from libc/ioctls/GTK to codec plugins — a long way from where this phase started.

## Round 53: the closure is complete on the Mac — and `docker cp` nested it into oblivion

What worked: the compat directory on the Mac now holds **524 files** — the six FFmpeg-8 libraries, the 17
codec libraries (18/18 of the runtime sonames verified in Round 52), Debian's `libgtk2.0-0t64` (which is
where `libgdk-x11-2.0.so.0` lives) and the GTK companions from `steam-gtk.tar.gz`. The container installed
its 35 packages cleanly (`pkgs-done`), and the image tarred at 859 M.

What did **not** work, and it is the whole of this round's failure:

```
compat=1                      <- only one entry inside /opt/steam-compat/usr/lib/aarch64-linux-gnu
steamui_missing=7             <- nothing was actually there
runtime: dlmopen … failed: libgtk-x11-2.0.so.0
```

`docker cp SRC DEST` copies **into** `DEST` when `DEST` already exists as a directory, so the 524 files
landed in `…/aarch64-linux-gnu/aarch64-linux-gnu/`. My own command created the destination first, which is
what triggered it. The fix is the standard idiom:

```sh
$D cp "$C/." "$id:/opt/steam-compat/usr/lib/aarch64-linux-gnu/"     # source "/.", trailing slash on dest
```

and the check that must pass before tarring:

```sh
$D exec "$id" sh -c 'echo compat=$(ls /opt/steam-compat/usr/lib/aarch64-linux-gnu | wc -l)'   # expect ~520
$D exec "$id" sh -c 'B=/root/.local/share/Steam; LP=$B/steamrtarm64:$B/steamrtarm64/libs:/opt/steam-compat/usr/lib/aarch64-linux-gnu;
    echo steamui=$(LD_LIBRARY_PATH=$LP ldd $B/steamrtarm64/steamui.so | grep -c "not found")'  # expect 0
```

**Do not reuse `arch-bp5.tar.gz`** — it was built with the nested copy and is known-bad.

Then the run, unchanged from Round 53's block. The three possible outcomes, all informative:
`xwininfo` lists Steam's window(s) → Big Picture (or the classic UI) is on screen on the Arch root;
`dlmopen` names another library → the closure is still one entry short and the name says which;
an assertion or a black window → the UI loaded but rendering is not usable at 1024×700 with llvmpipe.

This is the honest end state of Phase 2 from this session: **every input is verified and staged, the closure
was measured at 18/18, and the only thing that has not happened is a single correct `docker cp` followed by
one run.**

## Round 54: the dependency closure is CLOSED (0/0) — and Steam now exits silently

`evidence/41-bigpicture-answer2.txt`:

```
compat=524 steamui_missing=0 webhelper_missing=0      <- the whole closure resolves
TAR=858M                                              <- image built with it
steam alive: no
--- windows: 0 children
--- log: (nothing matched)
```

Two things, and the second one is my own reporting error:

* **The closure is closed.** 524 files in the guest's compat directory, and `ldd` reports **zero** missing
  libraries for both `steamui.so` and `steamwebhelper`. Every dependency question from Rounds 42-53 is
  answered; the six FFmpeg-8 libraries, the seventeen codecs, GTK2 and its GDK all resolve.
* **Steam now exits silently, and my grep hid the evidence.** The log pattern I chose this round
  (`dlmopen|fatal|assert|error|gamepad|Suppressing|…`) printed **nothing** — but in every earlier round the
  same command produced `Suppressing Steam update` and a `Startup - Steam Client launched with:` line. Both
  are absent, which means the log is empty or nearly so: the client died before logging, i.e. a *silent exit*
  (crash) rather than a handled failure. I filtered out the very lines that would have told me, and I did not
  print the exit status or the raw tail.

**Next step is therefore measurement again, not a fix** — read what actually happened:

```sh
# inside the guest, after the client exits:
echo "exit=$?"; wc -c /tmp/s.log; tail -30 /tmp/s.log          # the raw log, unfiltered
ls -la /tmp/dumps 2>/dev/null                                  # Steam's own minidumps
# and on the CLI side, look for a kernel crash report in the run's stderr, e.g.
#   "ish crash: pid N (steam) signal …" / "ish exit: pid N (steam) signal …"
```

Three likely shapes, each with a different follow-up: a **SIGSEGV in CEF/GTK2** (kernel-side gap or a missing
symbol — the CLI prints `ish crash:`); a **clean exit with a message** the log holds but my grep missed
(read it and act); or an **immediate `execve`/loader failure** (then `ldd` was right but the *runtime* loader
path differs — check `LD_LIBRARY_PATH` ordering and `steamrtarm64/libs`).

The image `arch-bp6.tar.gz` (858 M) is **good** — closure 0/0 verified inside it — so the next attempt can
reuse it directly: `fakefsify` is already done (`arch-bp6` exists), so only the run needs repeating with the
log kept unfiltered this time.

## Round 55: Steam segfaults before it logs — and my capture threw away the crash report

Two runs on the verified-good image (`arch-bp6`, closure 0/0), `evidence/42-steam-unfiltered.txt` and
`43-steam-tunables.txt`:

```
STATE=exited code=139          <- 128 + 11 = SIGSEGV
log bytes: 0                   <- it dies before writing anything, which is why Rounds 53/54 showed nothing
0 children                     <- no window, no partial UI
```

and with the **exact workarounds the Alpine build needed**, applied together
(`GLIBC_TUNABLES=glibc.pthread.stack_cache_size=0`, `MALLOC_ARENA_MAX=2`, `LP_NUM_THREADS=2`): **identical** —
exit 139, zero-byte log. So it is not that class of issue, and the tunables are ruled out.

**My capture error, again**: I piped through `tail -30` *before* `tee`, so the evidence files are 6 and 10
lines long and the kernel's own crash line never reached them. LinPad prints one for every guest crash —
the project's logs show the format `ish crash: pid N (comm) signal K … <library>+<offset>` — and that single
line names the subsystem: `libcef.so` (Valve's embedded browser), `steamui.so` (Valve's UI code), or the guest
loader/glibc (a kernel gap). It is the difference between three completely different next steps, and I threw
it away.

**Next step, exactly (one run):**

```sh
ish -f work/steam-distro/arch-bp6 /bin/sh -lc '…same run block…' > work/steam-distro/evidence/44-full.txt 2>&1
grep -nE 'ish crash|ish exit|signal|pc=|libcef|steamui|ld-linux' work/steam-distro/evidence/44-full.txt | head -20
```

**No `tail` before `tee` this time** — the whole stream to the file, then grep. Two cheap companions to that
run, in order: a **CEF smoke test** (`steamwebhelper -h` or `libcef.so`-only probe) to see whether the crash
is in CEF or in Valve's wrapper; and `steam -console` for a line the log would otherwise swallow.

Where this leaves Phase 2 honestly: the dependency graph is closed and measured (0/0), Steam reaches its own
startup and segfaults before logging, and the next run's *unfiltered* output is what says whether that crash
is a fixable kernel gap or a CEF/ARM64 limitation of this client under iSH. Nothing in the plan is blocked;
one run is simply owed, with better capture.

## Round 56: the CLI reports no crash detail, and the CEF probe tripped on `libcef.so`

Two attempts, both informative for the *next* run rather than this one:

1. **The `ish` CLI prints nothing for a guest crash.** A full-capture run (`evidence/44-full.txt`) is **three
   lines** — `STATE=exited code=139`, `log bytes: 0`, `R56_DONE` — with no `ish crash:` line. That reporting
   exists in the *app's* console (the Alpine logs have it), not in this CLI build, so the faulting offset
   cannot be obtained this way. (`ish --help` also does not exist: the CLI takes only its own short flags.)
2. **The CEF smoke test failed for a mundane reason** (`evidence/45-cef-probe.txt`):

```
$ LD_LIBRARY_PATH=$LP $B/steamrtarm64/steamwebhelper --version
steamwebhelper: error while loading shared libraries: libcef.so: cannot open shared object file
exit=127
```

Yet `ldd $B/steamrtarm64/libcef.so` reported **0 missing** earlier, i.e. the file exists at that path. So either
it is a **symlink whose target is missing**, its `DT_SONAME` differs from the file name, or the loader is
resolving it from a different search path than expected. The diagnostic that answers it in one line is at the
end of the transcript: `ls -la`, `readelf -d … | grep -i soname`, and
`find $B -name 'libcef.so*' -maxdepth 3`.

**Next step for the crash question, in order:**

1. Fix the CEF probe (the three commands above) and run `steamwebhelper --version` again. **If CEF itself
   segfaults (139), the problem is CEF-under-iSH** — a very different, kernel-level investigation than
   Valve's wrapper. **If it runs**, the crash is in Valve's `steam` startup path, and the next useful
   experiment is `strace`-style syscall tracing (`STrace` is compiled into the kernel and enabled by an
   environment/flag in the CLI) to see the last syscalls before the fault.
2. In parallel, the app-side route can answer it *better*: on the iPad, the guest's crash reporting **does**
   print (`ish crash: pid … signal … libcef.so+0x…`), so the same Steam run inside **LinPad** would name the
   faulting module directly. That makes the device run not just the acceptance step but the best diagnostic
   available for this crash.

## Round 56, the real finding: `libcef.so` is missing — and my `ldd` readings hid it

```
$ ls -la /root/.local/share/Steam/steamrtarm64/libcef.so
No such file or directory
$ find $B -name 'libcef.so*' -maxdepth 3
(nothing)
```

`libcef.so` **does not exist** in this client tree. And that exposes a mistake that has quietly poisoned
several rounds: I checked it with

```sh
LD_LIBRARY_PATH=$LP ldd $B/steamrtarm64/libcef.so 2>/dev/null | grep -c "not found"
```

When the file does not exist, `ldd` writes its error to **stderr**, which I sent to `/dev/null`, so the pipeline
counted **zero** matches — and I read "0". That is how "CEF needs nothing from the distro" (Rounds 43/44) and
"steamwebhelper missing=1, only libcef.so, which ships in steamrtarm64" (Rounds 44-54) both entered the record:
the first was a false negative, the second was an assumption I never checked (the `1 missing` was right, and my
explanation of it was wrong).

So the client's own CEF library is *absent*, while `steamui.so` is present and its dependencies now resolve
completely (0 missing, verified with the file present). Whether the SIGSEGV is "steamui tried to reach CEF and
died" or something else is exactly what the fixed probe will tell us — and the probe can now be run properly,
because the missing file is a **placed** problem, not a mystery: the Alpine build supplied CEF from
`work/steam/serve/steam-cef-glibc.tar.gz` (32 MB) and `steam-cef-extra.tar.gz`, which is why Steam worked
there and not here.

**Next step, precisely:** confirm which tarball carries `libcef.so` (the listing is in this round's
transcript), place it where the client expects (`steamrtarm64/`, or in the compat path on `LD_LIBRARY_PATH`),
re-run `steamwebhelper --version` as the CEF smoke test, and only then repeat the Big Picture run. If CEF then
loads and the client still segfaults, the fault is Valve's wrapper or an iSH kernel gap, and the iPad's own
crash reporting (`ish crash: pid … signal … libcef.so+0x…`) is the fastest way to name it.

## Round 56, conclusion: `libcef.so` is nowhere in the staged client — and my own workaround is the likely reason

The listing at the end of this round's transcript is unambiguous: `steam-client.tar.gz` contains
`steamrtarm64/{steamui.so, steamwebhelper, steamwebhelper.sh}` and **no `libcef.so`**, and neither
`steam-cef-glibc.tar.gz` nor `steam-cef-extra.tar.gz` carries one either. `find $B -name 'libcef.so*'`
finds nothing, at any depth.

Put that beside two facts already in the record:

* **Valve's Steam UI *is* CEF.** `steamui.so` is the UI layer on top of it, and `steamwebhelper` is the CEF
  host. Without `libcef.so` the client cannot draw its interface at all — which fits a SIGSEGV before any
  log line: the UI library loads, reaches for CEF, and dies.
* **On the Alpine device, Steam showed its sign-in screen** (build 4441) — so CEF existed there and worked.

Where would CEF have come from on the device? **The bootstrap update.** A fresh Steam install downloads its
client payload on first run — and I suppressed exactly that, with
`BootStrapperInhibitAll=enable`, because the CLI guest has no usable package/network path (Rounds 39-41).
The workaround that let me get further than the network error may be precisely what removed CEF from the
picture.

**So the head-side CLI test has reached its structural limit**, and it is my own limitation, not Arch's: this
harness cannot provide networking, so it cannot complete Steam's first-run payload, so the client it starts is
incomplete. The three things that follow from that:

1. **The `-gamepadui` question must be answered on the iPad.** There, networking exists, the bootstrap can
   complete (or has already completed — the device's Steam was installed and signed in previously), and the
   app's own crash reporting prints `ish crash: pid … signal … <library>+0x…`, which names any fault directly.
   This makes the device run **the diagnostic**, not merely the acceptance step.
2. **On the device, do not forbid the bootstrap**: drop `BootStrapperInhibitAll=enable` (it exists only for
   the networkless CLI) and let Steam fetch what it needs.
3. **Everything else is ready for that run**: the Arch image boots, the session layer runs, and the
   dependency closure for the client as shipped is measured at 0/0 with the seven-library compat layer
   staged — so if the device's Steam tree *does* have `libcef.so`, the same closure logic can be re-checked
   there in one command.

Phase 2's honest final state: **Arch Linux ARM hosts Valve's ARM64 client natively (0 missing libraries with
the compat layer), the client starts under `-gamepadui`, and the remaining crash is best explained by a CEF
payload this harness could not fetch.** The next meaningful step is the one the plan always had at the end:
run it on the iPad.

## Round 57: confirmed against the device's own crash reports — `libcef.so` belongs in `steamrtarm64/`

The Alpine-era evidence settles it (transcript above): every crash record from the working device names
`/root/.local/share/Steam/steamrtarm64/libcef.so+0x……` — the **exact path** my Arch image lacks — and the
device's `steamwebhelper` ran with a full CEF process tree (zygotes, network service, `cef_log.txt`). So:

* **The path is right and the file was there.** My Round-56 hypothesis holds: the client tree I staged is
  *incomplete* relative to the device's tree, and `BootStrapperInhibitAll=enable` — my networkless-harness
  workaround — is the most plausible reason it was never fetched here.
* **`ldd` was never the right check for it.** The false "0 missing" reading came from running `ldd` on a file
  that does not exist; from now on, `[ -e "$lib" ] && ldd "$lib" || echo ABSENT` — a missing file must never
  read as a satisfied dependency again.

**Where a copy could come from**, in order of preference:

1. **This Mac**, if `work/steam/**` holds one (the search is in the transcript above). The staging tree carries
   `cef-extra/` and the `debs` cache, so a `libcef.so` may already be sitting there.
2. **The iPad's existing Alpine root** — the file is known to be at
   `roots/default/data/root/.local/share/Steam/steamrtarm64/libcef.so` in the app-group container. Note
   `devicectl`'s container access is restricted to `Library`/`Documents`/`tmp`, so it cannot be copied out that
   way; it has to come through the app (a diagnostics/backup export), through the on-device transfer route, or
   by letting the device's Steam bootstrap do its job — which it can, because the iPad has networking.
3. **Valve**, by letting the client's own bootstrap fetch its payload on a networked system (the device), which
   is the natural fix and the reason Round 56's conclusion pointed at the iPad.

Everything else in the Arch image is verified: closure 0/0 for the *shipped* set, session layer running, image
booting. The only missing input is one library the harness could not obtain, and the plan's last step (run it
on the iPad, with the bootstrap allowed) is exactly what supplies it.

## Round 59: Phase 3, item 1 — the container-wide root trap, with the exact fix

Verified in the code (transcript above):

```objc
// app/Roots.m:294-296
- (BOOL)needsDefaultRoot { return self.roots.count == 0; }        // container-wide, not per app

// app/Roots.m:298-308
- (BOOL)importBundledRootWithProgress:… { … importRootFromArchive:archive name:@"default" … }

// app/Roots.m:100-107  (on init)
if ((!self.defaultRoot || ![self.roots containsObject:self.defaultRoot]) && self.roots.count)
    self.defaultRoot = self.roots.firstObject;                    // picks LinPad's Alpine root

// app/AppDelegate.m:478-485
if (Roots.instance.needsDefaultRoot) { …importBundledRootWithProgress:… }
```

So in a container where LinPad has already created its `default` (Alpine) root, the game app:
`needsDefaultRoot` → **false**, `defaultRoot` → **"default"**, and it boots **Alpine** — never importing its
own Arch image. The two-root design cannot work until this changes, and it is the first Phase 3 item.

**The fix, three small parts:**

1. `app/Roots.h/.m` — generalise the import and add a name query:
   ```objc
   - (BOOL)hasRootNamed:(NSString *)name;
   - (BOOL)importBundledRootNamed:(NSString *)name progress:(id<ProgressReporter>)progress error:(NSError **)error;
   // keep -importBundledRootWithProgress:error: as a call through with @"default"
   ```
2. `app/AppDelegate.m` — pick the name from the variant (the Info.plist key the build already sets) and import
   **that** root when it is missing, in both the synchronous path (`:478-485`) and the async one (`:813-859`):
   ```objc
   static NSString *VariantRootName(void) {
       NSString *v = [NSBundle.mainBundle objectForInfoDictionaryKey:@"LinPadVariant"];
       return [v isEqualToString:@"steam"] ? @"arch" : @"default";
   }
   NSString *root = VariantRootName();
   if (![Roots.instance hasRootNamed:root]) { import bundled into `root`; [Roots.instance setDefaultRoot:root]; }
   ```
   Note `defaultRoot` is persisted per app in `NSUserDefaults` (`Roots.m:113-117`), which is exactly what makes
   two apps with two roots in one container workable.
3. `app/CurrentRoot.m` — gate `FsUpdateRepositories()` / `FsUpdateOnlyRepositoriesFile()` (`:91-111`) on the
   booted root actually being Alpine (`[NSFileManager … fileExistsAtPath:<root>/data/etc/apk]`), so nothing
   writes `/etc/apk/repositories` into an Arch root. Likewise skip the Alpine repair kit for the Arch root
   (`RepairKit`/`SystemMaintenance`), which the game variant does not need.

**Verification available on this Mac**: a DesktopKit unit test for the variant→root-name mapping (the Swift
side already models `LinPadVariant`), plus a simulator build of the app target to prove the ObjC compiles.
The end-to-end proof (game app boots `roots/arch`, desktop keeps `roots/default`) is a device run.

Also still pending from the record: the per-root session lock (`roots/<name>/.session.lock` instead of one
lock for the container) and removing the temporary `FOOTIOCTL` print in `kernel/fs.c:753`.

## Round 67: the last code item — gate the Alpine repair kit off the Arch root, with its call sites

Located precisely (transcript above):

* `Core/System/Maintenance/SystemMaintenance.swift` — `kit: RepairKit?` (`:63`), `init(host:kit:defaults:)`
  (`:70`), the installed-version probe (`:92`), and the automatic path after boot (`:97-110`, "repairs
  silently once when the app bundles a newer kit"). `guestKitDirectory = "/tmp/linpad-repair-kit"` (`:68`).
* Triggered from `MaintenanceSettingsSection.swift:147` ("Repair System") and `:181` ("Repair Again").

The kit is **Alpine-specific** — `release/guest/linpad-repair` runs `apk info/add/update`, installs Firefox
ESR prefs, lifecycle scripts and the theme packs. The game app boots the Arch root, where none of that
belongs: it would print `apk: not found`-style failures and could half-apply files.

**The patch** (one guard + one small helper, in the same spirit as `FsIsAlpineRoot()` in `CurrentRoot.m` —
ask the guest, do not guess from the variant):

```swift
/// The bundled kit is the Alpine system's (apk, Firefox ESR, lifecycle, themes). The game app boots its
/// own Arch root, where it does not apply — ask the guest rather than trusting the variant.
private func rootIsAlpine() async -> Bool {
    (await host.run("[ -f /etc/apk/world ] && echo alpine")).stdout.contains("alpine")
}
```

then, at the top of `repair(automatic:)` and of the automatic path in `afterBoot`, `guard await rootIsAlpine()
else { … }` — returning the existing "nothing to do" outcome rather than an error, so the Settings row can
say the system does not use the Alpine kit instead of failing.

Verification available here: the DesktopKit suite (the mock host answers `run`, so the guard is unit-testable
in both directions), plus a simulator build. The end-to-end proof stays the device run.

With this, the only remaining items are this guard and **the iPad run**, which needs `libcef.so` — present
only on the device.

## Round 70: the deliverable — LinPad Steam build 4512 with the Arch root inside

Built from the final tree (`8ef6db8a`, every Phase 3 fix in it) with the Arch image as the embedded root:

```
LinPad-Steam-1.4.0-build4512.ipa   910,967,195 bytes
sha256 e2292dc739f04ec1b2631c98cb8e13297ec42002dc2954cdb939e1fcc55b4a7f
bundle id com.konradkern.linpad.steam   variant steam   1,047 members
```

The size tells the story: 910 MB against the Alpine-rooted build's 637 MB, the difference being the Arch
image (899 MB gzipped) in place of Alpine's (622 MB). `cut-ipa.py` verified every member against the staged
app, and the signature carries the shared app group, so installing it puts the Arch system in the same
container LinPad uses, under `roots/arch`, with the desktop's `roots/default` untouched.

**What this IPA does when installed**: the game app boots `roots/arch` (Phase 3 parts 1-2), locks it per root
so LinPad may run alongside (the per-root lock), never triggers the Alpine repair kit or apk state writes
against it (parts 3 and 4), and starts `steam-bigpicture` on the Arch root.

**What it will still hit**: the Steam UI needs `libcef.so`, which is absent from the staged client and exists
only in the device's existing Steam tree — so on first run expect the client to reach its startup and die, or
to fetch CEF itself if the bootstrap is allowed (do **not** set `BootStrapperInhibitAll=enable` on the device;
that flag existed only for the networkless Mac harness).

**Known gap**: `root.version` was not embedded (the Arch image has no sibling `.version` file), so the app
offers no OTA update for this root. Boot and import are unaffected — Phase 3 keys the import on
`hasRootNamed:`, not on versions.

Copied to the session workspace: `steamos/deliverables/LinPad-Steam-1.4.0-build4512.ipa` with its
`SHA256SUMS`, `ipa.json` and `build-facts.txt`.

## Round 71, correction: the IPA's Arch image is the CLI test image, not an image the app can run

**Do not use `LinPad-Steam-1.4.0-build4512.ipa` for the device test.** The image inside it
(`work/steam-distro/arch-bp6.tar.gz`) was built to run Steam under Xvnc from the Mac harness, and it is
missing everything the *app* needs:

```
$ tar -tzf arch-bp6.tar.gz | grep -E 'usr/local/bin/(steam-bigpicture|steam-local|ishwl|ishwl-session)|etc/ishwl|steam.desktop|linpad/steam-mode'
(nothing)
```

Yet `GameMode.swift:13` sets `steamCommand = "steam-bigpicture"` and the app shows guest windows *through
ishwl*, so on install the game app would boot Arch, fail to spawn its launcher, and after 180 s report
"Steam did not appear" — a failure that says nothing about Steam, CEF or Big Picture. My Round-70 sentence
"starts `steam-bigpicture` on the Arch root" was wrong: the script exists in the repository
(`release/guest/steam-bigpicture`, 4,327 bytes) but was never installed into that image.

### The image the app needs (one container pass, then build/sign/cut)

Everything from the Round 51/54 recipe **plus** the app's own guest files:

```sh
$D cp work/LinPad/release/guest/steam-bigpicture "$id:/usr/local/bin/steam-bigpicture"
$D cp work/LinPad/release/guest/steam-local     "$id:/usr/local/bin/steam-local"
$D cp work/LinPad/release/guest/steam.desktop   "$id:/usr/share/applications/steam.desktop"
$D cp work/LinPad/release/guest/steam.svg       "$id:/usr/share/icons/hicolor/scalable/apps/steam.svg"
# the glibc session layer built in Round 16/18, and the app displays windows through these:
$D cp work/steam-distro/guest-tools/arch/ishwl              "$id:/usr/local/bin/ishwl"
$D cp work/steam-distro/guest-tools/arch/ishwl-x11-wm       "$id:/usr/local/bin/ishwl-x11-wm"
$D cp work/steam-distro/guest-tools/arch/libishwl-compat.so "$id:/usr/local/lib/libishwl-compat.so"
for f in ishwl-session ishwl-launch ishwl-x11; do $D cp work/LinPad/wl-bridge/$f "$id:/usr/local/bin/$f"; done
$D cp work/LinPad/wl-bridge/apps "$id:/etc/ishwl/apps"
printf 'bigpicture\n' | $D exec -i "$id" sh -c 'mkdir -p /etc/linpad && cat > /etc/linpad/steam-mode'
# /etc/profile.d/gpu.sh with Arch's ICD path, /sbin/init, the Steam client, the compat layer,
# the ~/.steam symlinks -- but NOT steam.cfg's BootStrapperInhibitAll (that was a harness workaround).
# Also write /usr/share/ish/rootfs-version and a sibling root.version so the image is stamped.
```

Then: `build-local.sh steam` with `ROOTFS_ARCHIVE=<new image>` → `sign-variant.py --variant steam` →
`cut-ipa.py --variant steam`, and only then a device run. Note `libishwl-scm.so` is deliberately **absent**:
Round 19 proved glibc passes descriptors natively, so the musl-era shim must not be installed.

This is the true remaining work on the Mac side, and it is one container pass plus a build. The device run
comes after it, not before.
