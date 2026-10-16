#!/bin/sh
# Installs this tool's files, the EGL hook the tool defaults to, and — unless
# LINPAD_REAPER_STUB=0 — Steam's reaper replacement. Does not start Steam or edit its settings.
#
#   sh install.sh                      # into ~/.local/share/Steam (or $LINPAD_STEAM_ROOT)
#   LINPAD_REAPER_STUB=0 sh install.sh # leave Steam's own reaper alone
#
# Why the reaper: Steam launches a game as
#   steam-launch-wrapper -- reaper SteamLaunch AppId=… -- <tool>/run waitforexitandrun -- …
# and under iSH the reaper dies with "prctl() failed!" before the tool is reached, so every launch
# ends in the same second. The stub drops the "SteamLaunch AppId=… --" prefix and execs the rest.
# Steam's own reaper is kept as a timestamped backup under linpad-tool-backups/.
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
[ -f "$base/reaper-stub" ] || die "missing source: $base/reaper-stub"
[ -f "$base/linpad-egl-hook" ] || die "missing source: $base/linpad-egl-hook"
sh -n "$source_dir/run"
sh -n "$source_dir/launch-game"
sh -n "$base/reaper-stub"
sh -n "$base/linpad-egl-hook"
backups="$steam_root/linpad-tool-backups"

# The EGL hook first: launch-game defaults to this exact path, so a tool installed without it
# would die with "FEX executable unavailable".
prefix=${LINPAD_PREFIX:-/usr/local}
case "$prefix" in /*) ;; *) die 'LINPAD_PREFIX must be an absolute path' ;; esac
hook="$prefix/bin/linpad-egl-hook"
[ -d "$prefix/bin" ] || die "prefix bin directory missing: $prefix/bin"
[ -w "$prefix/bin" ] || die "cannot write $prefix/bin (run as the user that owns it)"
if [ -f "$hook" ] && cmp -s "$base/linpad-egl-hook" "$hook"; then
    printf 'Already installed: %s\n' "$hook"
else
    mkdir -p "$backups"
    if [ -e "$hook" ]; then
        cp -p "$hook" "$backups/linpad-egl-hook-$(date +%Y%m%dT%H%M%S)-$$"
    fi
    install -m 755 "$base/linpad-egl-hook" "$hook" 2>/dev/null || { cp "$base/linpad-egl-hook" "$hook" && chmod 755 "$hook"; }
    printf 'Installed: %s\n' "$hook"
fi
[ "$prefix" = /usr/local ] || printf '%s\n' \
    "Note: launch-game defaults to /usr/local/bin/linpad-egl-hook; with LINPAD_PREFIX=$prefix set LINPAD_FEX_BINARY=$hook when launching."

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
        tool_done=1
    fi
fi
if [ "${tool_done:-0}" -eq 0 ]; then
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
        backup="$backups/linpad-fex-native-$(date +%Y%m%dT%H%M%S)-$$"
        mkdir -p "$backups"
        mv "$target" "$backup"
    fi
    if ! mv "$staging" "$target"; then
        if [ -n "$backup" ]; then mv "$backup" "$target"; fi
        die 'could not place the new tool; prior installation restored when present'
    fi
    trap - EXIT HUP INT TERM
    printf 'Installed: %s\n' "$target"
    [ -z "$backup" ] || printf 'Previous tool saved: %s\n' "$backup"
fi

# Steam's reaper, unless asked not to. Its absence is not fatal: a Steam client that never
# launches a game does not need it, and the tool itself still works from the command line.
reaper="$steam_root/steamrtarm64/reaper"
if [ "${LINPAD_REAPER_STUB:-1}" = 0 ]; then
    printf 'Leaving Steam reaper alone (LINPAD_REAPER_STUB=0): %s\n' "$reaper"
elif [ ! -d "$steam_root/steamrtarm64" ]; then
    printf 'No steamrtarm64 directory; skipping the reaper stub: %s\n' "$steam_root/steamrtarm64"
elif [ -f "$reaper" ] && cmp -s "$base/reaper-stub" "$reaper"; then
    printf 'Already stubbed: %s\n' "$reaper"
else
    mkdir -p "$backups"
    if [ -f "$reaper" ]; then
        saved="$backups/reaper-$(date +%Y%m%dT%H%M%S)-$$"
        cp -p "$reaper" "$saved"
        printf 'Steam reaper saved: %s\n' "$saved"
    fi
    install -m 755 "$base/reaper-stub" "$reaper" 2>/dev/null || { cp "$base/reaper-stub" "$reaper" && chmod 755 "$reaper"; }
    printf 'Installed: %s (stub; Steam cannot run its own reaper under iSH)\n' "$reaper"
fi

printf '%s\n' 'Restart Steam when ready, then press Play — the mapping for appid 367520 is already in config.vdf.'
