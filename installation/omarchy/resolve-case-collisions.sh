#!/bin/sh
# Removes case-only duplicate paths from the image tree.
#
# The iPad's container and the Mac's disk are case-insensitive, so two paths that differ
# only in case share one backing file. The Arch tree is full of them (terminal aliases,
# man-page symlinks, xtables symlinks, perl's pod/Pod, xtables, and even
# /usr/bin/hyprland vs /usr/bin/Hyprland), and the importer cannot store both: it fails
# with EEXIST the moment the second name is created -- which is exactly how the first
# simulator import died (openat(/usr/share/man/man3/OSSL_TRACE_BEGIN.3ssl.gz): File exists).
#
# Rules, in order:
#   1. If one entry is a symlink that resolves to the other, keep the *lowercase* name and
#      materialise it as a real copy of the target, then drop the other. That keeps the
#      name every script uses (hyprland, not Hyprland) with real content behind it.
#   2. Otherwise keep the lowercase name and drop the other, logging it. Losing a
#      case-variant alias of a man page or a terminfo entry is acceptable; losing a real
#      file with different content is not, so those are listed in the output.
#
# Runs inside the container (a case-sensitive filesystem), from /.
#
#   sh installation/omarchy/resolve-case-collisions.sh [--dry-run]
set -eu

DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

cd /

find . \
    \( -path ./proc -o -path ./sys -o -path ./dev -o -path ./tmp -o -path ./run \
       -o -path ./root/.cache \) -prune -o -print |
    awk '{ key = tolower($0); if (key in first) print first[key] "\t" $0; else first[key] = $0 }' \
    >/tmp/case-collisions.txt

total=$(wc -l </tmp/case-collisions.txt | tr -d ' ')
echo "resolve-case-collisions: $total colliding pair(s)"
[ "$total" != 0 ] || exit 0

kept_lower=0
materialised=0
dropped_real=0

while IFS='	' read -r a b; do
    [ -n "${a:-}" ] || continue
    [ -n "${b:-}" ] || continue

    # Keep the name without uppercase letters; if both have some, keep the first.
    keep=$a
    drop=$b
    case $a in
        *[A-Z]*) keep=$b; drop=$a ;;
    esac

    if [ -L "$keep" ]; then
        # The kept (lowercase) name is a symlink. If it points at the entry we are about to
        # drop, put the real content behind the kept name first: that is the hyprland case
        # (/usr/bin/hyprland -> /usr/bin/Hyprland), and keeping the dangling link would
        # break every script that calls `hyprland`.
        keep_target=$(readlink -f -- "$keep" 2>/dev/null || true)
        drop_target=$(readlink -f -- "$drop" 2>/dev/null || true)
        if [ -n "$keep_target" ] && [ "$keep_target" = "$drop_target" ] && [ -e "$drop" ]; then
            if [ "$DRY" = 0 ]; then
                tmp=$keep.real.$$
                cp -a -- "$drop" "$tmp"
                rm -rf -- "$keep"
                mv -- "$tmp" "$keep"
                rm -rf -- "$drop"
            fi
            materialised=$((materialised + 1))
            printf 'materialised %s from %s\n' "$keep" "$drop"
            continue
        fi
        # An alias of some third object: dropping the other name loses nothing.
        if [ "$DRY" = 0 ]; then rm -rf -- "$drop"; fi
        kept_lower=$((kept_lower + 1))
        continue
    fi

    # Two real entries with different content (a terminfo directory, the Xorg binary, …).
    # Keep the lowercase one and say exactly what was lost.
    if [ "$DRY" = 0 ]; then rm -rf -- "$drop"; fi
    dropped_real=$((dropped_real + 1))
    printf 'dropped real entry: %s (kept %s)\n' "$drop" "$keep"
done </tmp/case-collisions.txt

echo "resolve-case-collisions: $kept_lower alias(es) removed, $materialised materialised, $dropped_real real entry(ies) dropped"

# The import cannot survive even one remaining pair, so verify rather than assume.
left=$(find . \
    \( -path ./proc -o -path ./sys -o -path ./dev -o -path ./tmp -o -path ./run \
       -o -path ./root/.cache \) -prune -o -print |
    awk '{ key = tolower($0); if (key in first) print first[key]; else first[key] = $0 }' | wc -l | tr -d ' ')
if [ "$left" != 0 ]; then
    echo "resolve-case-collisions: $left colliding pair(s) remain" >&2
    exit 1
fi
echo "resolve-case-collisions: tree is free of case collisions"
