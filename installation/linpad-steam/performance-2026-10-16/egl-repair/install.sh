#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
runtime=/opt/hollow-egl
fail() { printf 'EGL repair: %s\n' "$*" >&2; exit 1; }
# Add only to the separate EGL prefix after checking the unchanged base runtime.
[ -d /opt ] && [ ! -L /opt ] || fail 'unexpected /opt directory'
if [ -e "$runtime" ] || [ -L "$runtime" ]; then
    [ -d "$runtime" ] && [ ! -L "$runtime" ] || fail 'unexpected EGL prefix'
fi
sha256sum -c "$here/prerequisites.sha256" || fail 'base runtime or Mesa26 fingerprint does not match'
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
printf 'EGL_OVERLAY_INSTALLED\n'
