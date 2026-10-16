#!/bin/sh
# Fetch the compatibility layer the Arch guest needs to run Valve's ARM64 Steam client.
#
#   sh installation/steam-distro/fetch-arch-compat-layer.sh [output-dir]
#
# Why this exists at all (Round 45 onward): the client was built against an FFmpeg 8 vintage, and Arch
# has since moved to FFmpeg 9 (`libavcodec.so.63` where the client wants `.so.62`); Arch has also dropped
# GTK2, which `steamui.so` links. Everything else in the image comes from pacman. So this pulls exactly
# those sonames -- 6 FFmpeg libraries, their codec dependencies, and GTK2 with its GDK -- from Debian sid,
# which ships all three FFmpeg generations side by side (`libavcodec61/62/63`) precisely so consumers can
# pick their vintage, and whose GTK2 survived the time_t transition as `libgtk2.0-0t64`.
#
# It is a *compatibility layer*, not a second distribution: it goes in /opt/steam-compat on the launcher's
# LD_LIBRARY_PATH, and the packages and hashes are recorded next to it so the provenance is inspectable.
#
# Note on drift: sid moves, so this does not pin versions. The gate is the soname list at the bottom --
# if a future sid drops or renames something the client needs, this fails loudly instead of shipping an
# image that dies inside `dlmopen steamui.so`.
#
# IMPORTANT, learned the hard way (Round 74): the soname gate is *necessary but not sufficient*. It checks
# the names the client asks for directly, and the first version of this script passed it with 80 files --
# while `ldd steamui.so` in a guest still reported 9 missing, because the libraries it fetched have needs
# of their own (x265 -> libnuma; gnutls -> nettle/hogweed; rist -> cjson; lilv -> serd/sord/sratom). Those
# packages are in the list now, but the authoritative check is the client's own view:
#
#   $D exec "$id" sh -c 'B=/root/.local/share/Steam; LP=$B/steamrtarm64:$B/steamrtarm64/libs:/opt/steam-compat/usr/lib/aarch64-linux-gnu; \
#       LD_LIBRARY_PATH=$LP ldd $B/steamrtarm64/steamui.so | grep "not found"'
#
# which must print nothing. The 524-file layer the shipped image uses came from a full Debian GTK closure
# and therefore happened to be complete; this script aims to reproduce that completeness deliberately.
set -e

MIRROR=${MIRROR:-https://deb.debian.org/debian}
SUITE=${SUITE:-sid}
ARCH=${ARCH:-arm64}
OUT=${1:-$(cd "$(dirname "$0")/../.." && pwd)/work/steam-distro/compat/usr/lib/aarch64-linux-gnu}

# 6 + 17 + GTK2 (the t64 suffix is Debian's time_t transition, not a typo).
PACKAGES="libavcodec62 libavformat62 libavutil60 libswresample6 libswscale9 libavfilter11 \
libx265-216 libjxl0.11 librav1e0.8 libsvtav1enc4 libzvbi0t64 libcodec2-1.2 libshine3 libtwolame0 \
libgme0 libchromaprint1 librabbitmq4 librist4 libsrt1.5-gnutls liblilv-0-0 libmysofa1 libflite1 \
libgtk2.0-0t64 \
libnuma1 libnettle8 libhogweed6 libcjson1 libserd-0-0 libsord-0-0 libsratom-0-0"

# The sonames the client asks for. libgtk-x11-2.0 and libgdk-x11-2.0 are the pair `steamui.so` needs;
# `ldd` reports only the first missing one, so the check is deliberately exhaustive.
SONAMES="libavcodec.so.62 libavformat.so.62 libavutil.so.60 libswresample.so.6 libswscale.so.9 \
libavfilter.so.11 libx265.so.216 libjxl.so.0.11 libjxl_threads.so.0.11 librav1e.so.0.8 \
libSvtAv1Enc.so.4 libzvbi.so.0 libcodec2.so.1.2 libshine.so.3 libtwolame.so.0 libgme.so.0 \
libchromaprint.so.1 librabbitmq.so.4 librist.so.4 libsrt-gnutls.so.1.5 liblilv-0.so.0 libmysofa.so.1 \
libflite.so.1 libgtk-x11-2.0.so.0 libgdk-x11-2.0.so.0 \
libnuma.so.1 libnettle.so.8 libhogweed.so.6 libcjson.so.1 libserd-0.so.0 libsord-0.so.0 \
libsratom-0.so.0"

die() { echo "fetch-arch-compat-layer: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "needs $1 (macOS: brew install xz; ar and tar ship with the OS)"; }
need curl; need xz; need ar; need tar; need shasum

work=$(mktemp -d)
# shellcheck disable=SC2064
trap "rm -rf '$work'" EXIT INT TERM

echo "fetching the $SUITE/$ARCH package index from $MIRROR"
index=$work/Packages.xz
curl -fsSL --max-time 300 "$MIRROR/dists/$SUITE/main/binary-$ARCH/Packages.xz" -o "$index" \
    || die "could not fetch the package index"

mkdir -p "$OUT"
: > "$work/PROVENANCE.txt"
for p in $PACKAGES; do
    stanza=$(xz -dc "$index" | grep -A 30 "^Package: $p\$" || true)
    [ -n "$stanza" ] || die "$p is not in $SUITE/$ARCH (renamed, dropped, or moved out of main?)"
    file=$(printf '%s\n' "$stanza" | grep -m1 '^Filename:' | awk '{print $2}')
    version=$(printf '%s\n' "$stanza" | grep -m1 '^Version:' | awk '{print $2}')
    [ -n "$file" ] || die "$p has no Filename in the index"

    deb=$work/$(basename "$file")
    curl -fsSL --max-time 300 "$MIRROR/$file" -o "$deb" || die "could not fetch $file"
    printf '%s %s %s %s\n' "$p" "$version" "$(basename "$file")" "$(shasum -a 256 "$deb" | awk '{print $1}')" \
        >> "$work/PROVENANCE.txt"

    dir=$work/x/$(basename "$deb" .deb)
    mkdir -p "$dir"
    (cd "$dir" && ar x "$deb")
    data=$(ls "$dir"/data.tar.* 2>/dev/null | head -1)
    [ -n "$data" ] || die "$p has no data.tar member"
    tar -xf "$data" -C "$dir"
    # -type l matters: the runtime packages ship both the real file and its soname symlink, and copying
    # only regular files loses exactly the names the loader looks for (Round 52 -- 2 of 18 sonames
    # resolved until this was fixed).
    find "$dir" -name 'lib*.so*' \( -type f -o -type l \) -exec cp -a {} "$OUT/" \;
done

missing=
for s in $SONAMES; do
    [ -e "$OUT/$s" ] || missing="$missing $s"
done
[ -z "$missing" ] || die "the layer is incomplete, missing:$missing"

cp "$work/PROVENANCE.txt" "$OUT/../PROVENANCE.txt"
echo "wrote $(ls "$OUT" | wc -l | tr -d ' ') files to $OUT"
echo "all $(printf '%s\n' $SONAMES | wc -l | tr -d ' ') sonames present; provenance in $OUT/../PROVENANCE.txt"
