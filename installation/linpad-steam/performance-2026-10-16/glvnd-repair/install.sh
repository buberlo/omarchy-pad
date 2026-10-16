#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
runtime=/opt/hollow-runtime
fail() { printf 'GLVND repair: %s\n' "$*" >&2; exit 1; }
# Install only into the existing, fingerprint-matched x86 runtime.
for dir in "$runtime" "$runtime/usr" "$runtime/usr/lib" "$runtime/usr/lib/x86_64-linux-gnu"; do
    [ -d "$dir" ] && [ ! -L "$dir" ] || fail "unexpected runtime directory: $dir"
done
(cd "$runtime" && sha256sum -c "$here/runtime-fingerprint.sha256") || fail 'runtime fingerprint does not match'
(cd "$here/payload" && sha256sum -c "$here/payload-files.sha256") || fail 'payload checksum mismatch'
# Complete preflight before adding anything. Existing matching files are kept;
# any different file or link makes the whole operation stop before writes.
while read -r digest path; do
    target=$runtime/$path
    parent=$(dirname "$target")
    while [ "$parent" != "$runtime" ]; do
        [ ! -L "$parent" ] || fail "symlink parent: $parent"
        parent=$(dirname "$parent")
    done
    if [ -e "$target" ] || [ -L "$target" ]; then
        [ -f "$target" ] && [ ! -L "$target" ] || fail "unexpected existing target: $path"
        actual=$(sha256sum "$target"); actual=${actual%% *}
        [ "$actual" = "$digest" ] || fail "refusing to replace existing file: $path"
    fi
done < "$here/payload-files.sha256"
while read -r path link; do
    [ -L "$here/payload/$path" ] && [ "$(readlink "$here/payload/$path")" = "$link" ] || fail "unexpected payload link: $path"
    target=$runtime/$path
    if [ -e "$target" ] || [ -L "$target" ]; then
        [ -L "$target" ] && [ "$(readlink "$target")" = "$link" ] || fail "refusing to replace existing link: $path"
    fi
done < "$here/payload-symlinks.txt"
while read -r digest path; do
    target=$runtime/$path
    if [ ! -e "$target" ]; then
        mkdir -p "$(dirname "$target")"
        cp -p "$here/payload/$path" "$target"
        printf 'ADDED %s\n' "$path"
    fi
done < "$here/payload-files.sha256"
while read -r path link; do
    target=$runtime/$path
    if [ ! -L "$target" ]; then
        ln -s "$link" "$target"
        printf 'ADDED %s -> %s\n' "$path" "$link"
    fi
done < "$here/payload-symlinks.txt"
(cd "$runtime" && sha256sum -c "$here/payload-files.sha256")
printf 'GLVND_REPAIR_INSTALLED\n'
