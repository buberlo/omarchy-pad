#!/bin/sh
# Installs this tool's four files only. Does not start Steam or edit its settings.
set -eu
die() { printf 'LinPad FEX install: %s\n' "$*" >&2; exit 1; }
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir="$base/linpad-fex-native"
steam_root=${LINPAD_STEAM_ROOT:-}
if [ -z "$steam_root" ]; then
    for candidate in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.steam/root"; do
        if [ -d "$candidate/steamapps" ]; then
            steam_root=$candidate
            break
        fi
    done
fi
[ -n "$steam_root" ] || die 'Steam directory not found; set LINPAD_STEAM_ROOT to its absolute path'
case "$steam_root" in /*) ;; *) die 'LINPAD_STEAM_ROOT must be an absolute path' ;; esac
[ -d "$steam_root/steamapps" ] || die "Steam library directory missing: $steam_root/steamapps"
for name in compatibilitytool.vdf toolmanifest.vdf run launch-game; do
    [ -f "$source_dir/$name" ] || die "missing source: $source_dir/$name"
done
sh -n "$source_dir/run"
sh -n "$source_dir/launch-game"
tools_root="$steam_root/compatibilitytools.d"
target="$tools_root/linpad-fex-native"
mkdir -p "$tools_root"
[ ! -L "$target" ] || die "refusing to replace a tool symlink: $target"
[ ! -e "$target" ] || [ -d "$target" ] || die "tool path is not a directory: $target"
if [ -d "$target" ]; then
    same=1
    for name in compatibilitytool.vdf toolmanifest.vdf run launch-game; do
        cmp -s "$source_dir/$name" "$target/$name" || same=0
    done
    if [ "$same" -eq 1 ] && [ -x "$target/run" ] && [ -x "$target/launch-game" ]; then
        printf 'Already installed: %s\n' "$target"
        exit 0
    fi
fi
staging=$(mktemp -d "$tools_root/.linpad-fex-native.XXXXXX")
trap 'rm -rf "$staging"' EXIT
trap 'exit 1' HUP INT TERM
for name in compatibilitytool.vdf toolmanifest.vdf run launch-game; do
    cp "$source_dir/$name" "$staging/$name"
done
chmod 644 "$staging/compatibilitytool.vdf" "$staging/toolmanifest.vdf"
chmod 755 "$staging/run" "$staging/launch-game"
backup=
if [ -d "$target" ]; then
    backup="$steam_root/linpad-tool-backups/linpad-fex-native-$(date +%Y%m%dT%H%M%S)-$$"
    mkdir -p "$steam_root/linpad-tool-backups"
    mv "$target" "$backup"
fi
if ! mv "$staging" "$target"; then
    if [ -n "$backup" ]; then mv "$backup" "$target"; fi
    die 'could not place the new tool; prior installation restored when present'
fi
trap - EXIT HUP INT TERM
printf 'Installed: %s\n' "$target"
[ -z "$backup" ] || printf 'Previous tool saved: %s\n' "$backup"
printf '%s\n' 'Restart Steam when ready, then select LinPad FEX (Linux Vulkan) for Hollow Knight.'
