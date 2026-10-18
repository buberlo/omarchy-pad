#!/bin/sh
# Imports an image tar into a fakefs and boots it with the macOS iSH CLI, then runs a
# probe. This is the Gate 0.2 harness: it proves an image boots before an iPad is
# involved, and it records what it saw under <repo>/work/omarchy-pad/evidence.
#
#   sh installation/omarchy/boot-test.sh IMAGE.tar.gz ["guest shell command"]
#
# Env:
#   WORK   work directory (default <repo>/work/omarchy-pad)
#   ISH    emulator CLI (default <repo>/build-arm64-release/ish)
#   KEEP   KEEP=1 reuses an existing fakefs instead of importing again
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
WORK=${WORK:-$ROOT/work/omarchy-pad}
ISH=${ISH:-$ROOT/build-arm64-release/ish}
FAKEFSIFY=${FAKEFSIFY:-$ROOT/build-arm64-release/tools/fakefsify}
TAR=${1:?usage: boot-test.sh IMAGE.tar.gz ["command"]}
PROBE=${2:-'cat /etc/os-release | head -4; uname -a; ls /usr/share/omarchy/bin/omarchy; command -v hyprland quickshell foot ishwl; echo OMARCHY_PAD_BOOT_OK'}

name=$(basename "$TAR" .tar.gz)
FS=$WORK/fs-$name
LOG=$WORK/evidence/boot-$name.txt
mkdir -p "$WORK/evidence"

[ -x "$ISH" ] && [ -x "$FAKEFSIFY" ] || { echo "$0: build ish and fakefsify first" >&2; exit 1; }

if [ "${KEEP:-0}" != 1 ] || [ ! -d "$FS" ]; then
    echo "importing $TAR -> $FS"
    rm -rf "$FS"
    "$FAKEFSIFY" "$TAR" "$FS"
fi

echo "booting $FS"
{
    echo "== $(basename "$TAR") $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    "$ISH" -f "$FS" /bin/sh -lc "$PROBE"
    echo "== exit $?"
} 2>&1 | tee "$LOG"

grep -q OMARCHY_PAD_BOOT_OK "$LOG" || { echo "$0: probe did not report success (see $LOG)" >&2; exit 1; }
echo "ok: $LOG"
