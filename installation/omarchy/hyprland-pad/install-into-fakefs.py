#!/usr/bin/env python3
"""Install Arch packages (and loose files) into an iSH fakefs.

    python3 install-into-fakefs.py --fs FS [--register PATH]... PKG.tar.zst...

Why this exists: a fakefs is a real directory tree (`FS/data`) *plus* `meta.db`, and
iSH's runtime resolves every path through meta.db (`fs/fake-db.c`: "select inode from
paths where path = ?"). Files that are merely dropped into `FS/data` are invisible
inside the guest -- `bsdtar -xf pkg.tar.zst -C FS/data` alone does **not** install
anything. This tool extracts the package and registers every entry it creates (or
updates the mode of an entry it replaces), so the guest actually sees it.

It never needs the emulator to be running; run it while the fakefs is idle.

Layout of the relevant tables:
    meta  (db_inode integer)                     -- inode of meta.db at last open
    stats (inode integer primary key, stat blob) -- struct ish_stat{mode,uid,gid,rdev}
    paths (path blob primary key, inode integer)
"""
import argparse
import os
import sqlite3
import stat as statmod
import struct
import subprocess
import sys

S_IFMT = 0o170000
S_IFREG = 0o100000
S_IFDIR = 0o040000
S_IFLNK = 0o120000


def ish_stat(mode, uid=0, gid=0, rdev=0):
    return struct.pack("<IIII", mode & 0xFFFFFFFF, uid & 0xFFFFFFFF, gid & 0xFFFFFFFF, rdev & 0xFFFFFFFF)


def mode_for(real_path, tar_mode=None):
    """Mode to record for a path that already exists on the host side."""
    st = os.lstat(real_path)
    if statmod.S_ISLNK(st.st_mode):
        kind = S_IFLNK
        # symlink permissions are meaningless on Linux; the image records 0777
        return kind | 0o777
    elif statmod.S_ISDIR(st.st_mode):
        kind = S_IFDIR
    else:
        kind = S_IFREG
    perms = (tar_mode if tar_mode is not None else st.st_mode) & 0o7777
    return kind | perms


class Fakefs:
    def __init__(self, fs):
        self.fs = os.path.abspath(fs)
        self.root = os.path.join(self.fs, "data")
        self.db_path = os.path.join(self.fs, "meta.db")
        if not os.path.isdir(self.root):
            sys.exit(f"{self.fs}: no data/ directory")
        if not os.path.isfile(self.db_path):
            sys.exit(f"{self.fs}: no meta.db")
        self.db = sqlite3.connect(self.db_path)
        self.cur = self.db.cursor()
        # a few (path -> inode) entries are shared by hardlinks; remember them per package
        self.hardlinks = {}
        self.added = self.updated = self.skipped = 0

    # --- meta.db helpers -------------------------------------------------
    def inode_of(self, path):
        row = self.cur.execute("select inode from paths where path = ?", (path.encode(),)).fetchone()
        return row[0] if row else None

    def register(self, path, mode, uid=0, gid=0, rdev=0):
        """Make `path` (guest absolute path) visible, creating or updating its entry."""
        ino = self.inode_of(path)
        if ino is None:
            self.cur.execute("insert into stats (stat) values (?)", (ish_stat(mode, uid, gid, rdev),))
            ino = self.cur.lastrowid
            self.cur.execute("insert into paths (path, inode) values (?, ?)", (path.encode(), ino))
            self.added += 1
        else:
            self.cur.execute("update stats set stat = ? where inode = ?", (ish_stat(mode, uid, gid, rdev), ino))
            self.updated += 1
        return ino

    def hardlink(self, path, other_path):
        """Point `path` at the same inode as `other_path` (tar hardlink entry)."""
        ino = self.inode_of(other_path)
        if ino is None:
            return False
        self.cur.execute("insert or replace into paths (path, inode) values (?, ?)", (path.encode(), ino))
        self.added += 1
        return True

    # --- installing ------------------------------------------------------
    def package_paths(self, pkg):
        """Guest paths in a package, via bsdtar (python's tarfile cannot read zstd here)."""
        out = subprocess.run(["bsdtar", "-tf", pkg], capture_output=True, text=True, check=True).stdout
        for line in out.splitlines():
            raw = line.strip()
            if not raw or raw.startswith(".") or raw == "./":
                continue
            name = raw[2:] if raw.startswith("./") else raw
            if not name:
                continue
            yield name

    def package_modes(self, pkg):
        """Guest path -> tar permission bits.

        The extraction happens through bsdtar on the host, so a file's mode on disk is the
        tar's mode filtered by the host umask: binaries came out 0644, iSH registered that,
        and the guest then answered "Permission denied" for e.g. hyprctl -- which broke every
        Omarchy script that shells out to it. The tar is the authority, so take the bits from
        there and put them on the extracted file as well.
        """
        modes = {}
        out = subprocess.run(["bsdtar", "-tvf", pkg], capture_output=True, text=True, check=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) < 9:
                continue
            perms, name = parts[0], parts[-1]
            if name.startswith("./"):
                name = name[2:]
            if not name or perms[0] not in "-dl":
                continue
            bits = 0
            for i, ch in enumerate(perms[1:10]):
                if ch in "rwxstT":
                    bits |= {0: 0o400, 1: 0o200, 2: 0o100, 3: 0o040, 4: 0o020, 5: 0o010,
                             6: 0o004, 7: 0o002, 8: 0o001}[i]
            modes[name] = bits
        return modes

    def install_package(self, pkg):
        # hardlinked members share the real inode after extraction: register the first
        # path normally and point the rest at that same inode.
        seen = {}
        before = (self.added, self.updated, self.skipped)
        modes = self.package_modes(pkg)
        fixed_modes = 0

        for name in self.package_paths(pkg):
            guest = "/" + name
            real = os.path.join(self.root, name)

            if not os.path.lexists(real):
                self.skipped += 1
                print(f"  !! {guest}: not on disk after extraction (skipped)")
                continue

            # Give the extracted file the mode the package declares: the host umask otherwise
            # strips the executable bits and the guest cannot run it (see package_modes).
            want = modes.get(name)
            if want is not None and statmod.S_ISREG(os.lstat(real).st_mode):
                have = statmod.S_IMODE(os.lstat(real).st_mode)
                if have != want:
                    os.chmod(real, want)
                    fixed_modes += 1

            st = os.lstat(real)
            key = (st.st_dev, st.st_ino)
            if statmod.S_ISREG(st.st_mode) and st.st_nlink > 1 and key in seen:
                self.hardlink(guest, seen[key])
                continue

            if statmod.S_ISREG(st.st_mode) and st.st_nlink > 1:
                seen[key] = guest

            self.register(guest, mode_for(real))

        print(f"  {os.path.basename(pkg)}: {self.added - before[0]} new, "
              f"{self.updated - before[1]} updated, {self.skipped - before[2]} skipped, "
              f"{fixed_modes} mode(s) fixed from the package")

    def register_file(self, path, mode=None):
        """Register a file that was copied into FS/data by hand.

        --mode only overrides the permission bits; the file type always comes from the
        file itself (a mode without S_IFREG makes the guest see a non-regular file, and
        e.g. Hyprland then refuses its config with "not a regular file").
        """
        guest = "/" + path.lstrip("/")
        real = os.path.join(self.root, path.lstrip("/"))
        if not os.path.lexists(real):
            sys.exit(f"register: {real} does not exist")

        resolved = mode_for(real)
        if mode is not None:
            resolved = (resolved & S_IFMT) | (mode & 0o7777)

        self.register(guest, resolved)
        print(f"  registered {guest} ({oct(resolved)})")

    def commit(self):
        # keep meta.db's recorded inode in sync, otherwise iSH rebuilds the db on boot
        self.db.commit()
        st = os.stat(self.db_path)
        self.cur.execute("update meta set db_inode = ?", (st.st_ino,))
        self.db.commit()
        self.db.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fs", required=True, help="fakefs directory (contains data/ and meta.db)")
    ap.add_argument("--register", action="append", default=[], help="guest path of a file to register (repeatable)")
    ap.add_argument("--mode", help="octal mode for --register files (default: keep)")
    ap.add_argument("packages", nargs="*", help=".pkg.tar.zst files")
    args = ap.parse_args()

    fs = Fakefs(args.fs)
    mode = int(args.mode, 8) if args.mode else None

    for pkg in args.packages:
        print(f"== {pkg}")
        fs.install_package(pkg)

    for path in args.register:
        fs.register_file(path, mode)

    fs.commit()
    print(f"OK: {fs.added} new entries, {fs.updated} updated, {fs.skipped} skipped")


if __name__ == "__main__":
    main()
