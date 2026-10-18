#!/bin/sh
# Gate 0.4: run the patched Hyprland in the emulator on one headless output.
#
#   sh run-gate04.sh [zink|llvmpipe]      (default zink)
#
# Steps:
#   1. extract hyprland-pad + aquamarine-pad into $FS/data and register every file in
#      meta.db with install-into-fakefs.py (without registration the guest cannot see
#      them at all -- see that script's comment),
#   2. work around the one case collision in the Hyprland package: macOS is
#      case-insensitive, so usr/bin/Hyprland and its symlink usr/bin/hyprland cannot
#      both exist; the binary is installed as usr/bin/hyprland and meta.db maps both
#      guest paths to it,
#   3. copy the guest probe + config in and register them,
#   4. boot the GPU-enabled iSH CLI and run the probe,
#   5. copy the artifacts (log, PNG, raw shm frame) into $EVIDENCE and print pixel
#      statistics for the PNG (png-stats.py).
#
# Env:
#   FS        fakefs to use (default <repo>/work/omarchy-pad/fs-gate04)
#   ISH       emulator CLI (default <repo>/work/LinPad/build-arm64-release-gpu/ish)
#   GATE      gate dir with pkgs/ (default <repo>/work/omarchy-pad/gate-0.4)
#   EVIDENCE  output dir (default <repo>/work/omarchy-pad/evidence/gate-0.4)
#   RUN_TIMEOUT seconds for the whole guest run (default 3600)
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../.." && pwd)
GATE=${GATE:-$ROOT/work/omarchy-pad/gate-0.4}
FS=${FS:-$ROOT/work/omarchy-pad/fs-gate04}
ISH=${ISH:-$ROOT/work/LinPad/build-arm64-release-gpu/ish}
EVIDENCE=${EVIDENCE:-$ROOT/work/omarchy-pad/evidence/gate-0.4}
RENDERER=${1:-zink}
RUN_TIMEOUT=${RUN_TIMEOUT:-3600}

mkdir -p "$EVIDENCE" "$FS/data/tmp" "$FS/data/root/gate04-out"

AQPKG=$(ls "$GATE"/pkgs/aquamarine-pad-*.pkg.tar.zst)
HYPPKG=$(ls "$GATE"/pkgs/hyprland-pad-*.pkg.tar.zst)

################################################################################
# 1. packages -> fakefs
################################################################################
echo "== installing packages into $FS/data"

MANIFEST=$EVIDENCE/00-fs-manifest.txt
if [ ! -f "$MANIFEST" ]; then
    {
        echo "# files installed into $FS/data by run-gate04.sh ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
        echo "# packages:"
        ls -la "$AQPKG" "$HYPPKG"
        echo "# overwritten pre-existing files (restore from the image tar to revert):"
        for p in "$AQPKG" "$HYPPKG"; do
            bsdtar -tf "$p" | sed 's:/$::' | while read -r f; do
                case "$f" in .*) continue ;; esac
                [ -e "$FS/data/$f" ] && echo "$f"
            done
        done
        echo "# added paths (not in the image): see install-into-fakefs.py output"
    } > "$MANIFEST"
fi

# bsdtar keeps the modes; the two packages conflict only on usr/bin/Hyprland|hyprland
bsdtar -xf "$AQPKG" -C "$FS/data" --exclude '.PKGINFO' --exclude '.MTREE' --exclude '.BUILDINFO'
bsdtar -xf "$HYPPKG" -C "$FS/data" --exclude '.PKGINFO' --exclude '.MTREE' --exclude '.BUILDINFO' \
    --exclude 'usr/bin/Hyprland' --exclude 'usr/bin/hyprland'

TMP=$(mktemp -d)
bsdtar -xf "$HYPPKG" -C "$TMP" usr/bin/Hyprland
mv "$TMP/usr/bin/Hyprland" "$FS/data/usr/bin/hyprland"
chmod 755 "$FS/data/usr/bin/hyprland"
rm -rf "$TMP"

################################################################################
# 2. register in meta.db + guest files
################################################################################
cp "$HERE/guest/gate04-probe.sh"   "$FS/data/tmp/gate04-probe.sh"
cp "$HERE/guest/pad-hyprland.conf" "$FS/data/tmp/pad-hyprland.conf"
chmod +x "$FS/data/tmp/gate04-probe.sh"

python3 "$HERE/install-into-fakefs.py" --fs "$FS" "$AQPKG" "$HYPPKG" \
    --register /tmp/gate04-probe.sh --register /tmp/pad-hyprland.conf --mode 755 | tail -4

################################################################################
# 3. run
################################################################################
echo "== guest: $ISH -f $FS  (renderer: $RENDERER)"
set +e
if [ "$RENDERER" = llvmpipe ]; then
    timeout "$RUN_TIMEOUT" "$ISH" -f "$FS" /bin/sh -c \
        "LIBGL_ALWAYS_SOFTWARE=1 MESA_LOADER_DRIVER_OVERRIDE= MESA_GLES_VERSION_OVERRIDE=3.2 /bin/sh /tmp/gate04-probe.sh" \
        2>&1 | tee "$EVIDENCE/run-$RENDERER.txt"
else
    timeout "$RUN_TIMEOUT" "$ISH" -f "$FS" /bin/sh /tmp/gate04-probe.sh 2>&1 | tee "$EVIDENCE/run-$RENDERER.txt"
fi
RC=$?
set -e
echo "== emulator exit: $RC"

################################################################################
# 4. artifacts
################################################################################
echo "== collecting artifacts"
for f in hypr.log shot.png shot2.png frame.raw; do
    if [ -f "$FS/data/root/gate04-out/$f" ]; then
        cp "$FS/data/root/gate04-out/$f" "$EVIDENCE/"
        ls -la "$EVIDENCE/$f"
    fi
done

if [ -f "$EVIDENCE/shot.png" ]; then
    python3 "$HERE/png-stats.py" "$EVIDENCE/shot.png" "$EVIDENCE/frame.raw" 2>&1 | tee "$EVIDENCE/png-stats.txt"
fi

echo "== probe completion markers: $(grep -c 'GATE04_PROBE_DONE' "$EVIDENCE/run-$RENDERER.txt" 2>/dev/null || echo 0)"
