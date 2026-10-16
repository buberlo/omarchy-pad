#!/bin/sh
# Read-only: inspect only Hollow Knight's install manifest and ELF header.
set -eu
die() { printf 'Hollow Knight: %s\n' "$*" >&2; exit 1; }
library=${1:-${LINPAD_STEAM_ROOT:-}}
if [ -z "$library" ]; then
    for candidate in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.steam/root"; do
        if [ -f "$candidate/steamapps/appmanifest_367520.acf" ]; then
            library=$candidate
            break
        fi
    done
fi
[ -n "$library" ] || die 'pass the Steam library directory containing steamapps'
case "$library" in /*) ;; *) die 'Steam library path must be absolute' ;; esac
manifest="$library/steamapps/appmanifest_367520.acf"
[ -f "$manifest" ] || die "install manifest missing: $manifest"
awk '
    /"InstalledDepots"/ { section=1; next }
    section {
        text=$0; opens=gsub(/\{/, "", text); closes=gsub(/\}/, "", text)
        depth+=opens
        if (depth==1 && $0 ~ /"367523"/) found=1
        depth-=closes
        if (depth==0 && closes) exit
    }
    END { exit !found }
' "$manifest" || die 'Linux depot 367523 is not recorded in InstalledDepots'
install_dir=$(awk -F '"' '$2=="installdir" { print $4; exit }' "$manifest")
[ -n "$install_dir" ] || die 'installdir missing from the game manifest'
case "$install_dir" in /*|..|../*|*/../*|*/..) die 'unexpected installdir path' ;; esac
game="$library/steamapps/common/$install_dir/hollow_knight.x86_64"
[ -f "$game" ] || die "native game executable missing: $game"
header=$(od -An -tx1 -N20 "$game" | tr -d ' \n')
case "$header" in
    7f454c460201*3e00) ;;
    *) die 'game header is not a 64-bit little-endian x86 ELF' ;;
esac
printf '%s\n' 'PASS: Linux depot 367523 appears in InstalledDepots.'
printf 'PASS: x86-64 Linux ELF: %s\n' "$game"
printf '%s\n' 'This checks installed files only; launch, rendering and gameplay need separate verification.'
