#!/bin/sh
# Checks the launch channel between ishwl and the supervisor (omarchy-pad-init) without an
# emulator, in a container:
#
#   sh installation/omarchy/launch-channel-test/run.sh
#   -> work/omarchy-pad/evidence/gate-0.6/launch-channel.txt
#
# Why this exists: inside an app guest, a `fork()`ed guest process cannot `execve()` at all
# (the ENOTDIR bug in docs/omarchy/GATES.md), so an `ishwl` spawn becomes a request over the
# FIFO that PID 1 reads, and PID 1's own fork/exec starts the child. The pieces that can be
# wrong there are all checkable here, and a bug in any of them costs a full guest-image and
# app cycle (~20 min) to see:
#
#   * the supervisor's reader must survive the EOF it gets whenever a writer closes
#     (a bare `while read; done <fifo` loop ends after the first request);
#   * a request that arrives while no reader is attached must not be lost: the writer
#     waits out the supervisor's reopen (ENXIO) and a missing channel (ENOENT);
#   * the child must get ishwl's environment, not PID 1's, including values with quotes;
#   * launcher scripts must not pile up in /run.
#
# The supervisor loop and write_launch_script() are extracted from the real sources, so this
# cannot pass against code that has drifted. The writer's retry block in spawn-writer.c is a
# copy of the one in spawn_command() (same messages), because the rest of that function needs
# the compositor around it.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$here/../../.." && pwd)          # the LinPad checkout
WS=$(cd "$ROOT/../.." && pwd)               # the workspace that holds work/
WORK=${WORK:-$WS/work/omarchy-pad}          # where the gate evidence lives
DOCKER_CONTEXT=${DOCKER_CONTEXT:-colima-linpad-fex}
D="docker --context $DOCKER_CONTEXT"
IMAGE=${IMAGE:-menci/archlinuxarm:latest}
ZIG=${ZIG:-$WS/work/tools/venv/lib/python3.15/site-packages/ziglang/zig}
CONTAINER=omarchy-pad-launch-test
OUTDIR=$WORK/evidence/gate-0.6
EVIDENCE=$OUTDIR/launch-channel.txt
mkdir -p "$OUTDIR"

build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cp "$here/spawn-writer.c" "$here/run-test.sh" "$here/run-test2.sh" "$here/run-test3.sh" "$build/"

# 1. write_launch_script(), verbatim.
sed -n '/^static const char \*write_launch_script/,/^}/p' "$ROOT/wl-bridge/src/main.c" > "$build/writer.inc"
grep -q 'spawn-%d.sh' "$build/writer.inc" || {
    echo "$0: could not extract write_launch_script from wl-bridge/src/main.c" >&2; exit 1; }

# 2. The supervisor's launch loop, verbatim, with the script's helpers in front.
{
    printf '%s\n' '#!/bin/sh' 'RUNDIR=/run/omarchy-pad' 'LOG=$RUNDIR/init.log'
    printf '%s\n' 'mkdir -p "$RUNDIR"' ': >"$LOG"'
    printf '%s\n' 'log() { printf "%s %s\n" "$(date -u +%H:%M:%S)" "$*" >>"$LOG"; }'
    printf '%s\n' 'PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin' 'export PATH'
    sed -n '/^LAUNCH_FIFO=\$RUNDIR\/launch/,$p' "$ROOT/omarchy-pad/files/usr/local/bin/omarchy-pad-init"
} > "$build/mock-init.sh"
grep -q 'while :; do' "$build/mock-init.sh" || {
    echo "$0: the supervisor loop no longer reopens its FIFO (omarchy-pad-init)" >&2; exit 1; }

# 3. Cross-compile the writer (the guest is aarch64 Linux; the host is not).
"$ZIG" cc -target aarch64-linux-musl -O1 -o "$build/spawn-writer" "$build/spawn-writer.c"

# 4. Run it in the container that builds the guest image.
$D rm -f "$CONTAINER" >/dev/null 2>&1 || true
$D run -d --name "$CONTAINER" "$IMAGE" sleep 600 >/dev/null
$D exec "$CONTAINER" mkdir -p /work
for f in spawn-writer mock-init.sh run-test.sh run-test2.sh run-test3.sh; do
    $D cp "$build/$f" "$CONTAINER:/work/"
done

{
    printf 'launch channel: ishwl -> %s -> supervisor\n' '/run/omarchy-pad/launch'
    printf 'image %s, context %s\n' "$IMAGE" "$DOCKER_CONTEXT"
    printf 'ishwl write_launch_script: %s\n' "$(shasum -a 256 "$build/writer.inc" | cut -d' ' -f1)"
    printf 'supervisor loop: %s\n' "$(shasum -a 256 "$build/mock-init.sh" | cut -d' ' -f1)"
    printf '\n== sequential requests (reader must survive writer EOF)\n'
    $D exec "$CONTAINER" sh -c 'sh /work/run-test.sh 2>&1'
    printf '\n== races and bounded failure\n'
    $D exec "$CONTAINER" sh -c 'sh /work/run-test2.sh 2>&1; sh /work/run-test3.sh 2>&1'
} > "$EVIDENCE" 2>&1
$D rm -f "$CONTAINER" >/dev/null 2>&1 || true

# 5. Verdict: every scenario's marker must say it arrived, and nothing may be left in /run.
status=0
for marker in first second third a-ok b-ok; do
    grep -q "$marker" "$EVIDENCE" || { echo "$0: $marker missing from the run" >&2; status=1; }
done
if sed -n '/== leftovers/,$p' "$EVIDENCE" | grep -q 'spawn-'; then
    echo "$0: launcher scripts were left behind in /run/omarchy-pad" >&2; status=1
fi
grep -q 'request delivered after ' "$EVIDENCE" || { echo "$0: no request was delivered" >&2; status=1; }
grep -q 'launch channel open failed after 500ms' "$EVIDENCE" || {
    echo "$0: the no-supervisor case did not fail in bounded time" >&2; status=1; }

cat "$EVIDENCE"
[ "$status" = 0 ] || exit 1
printf '\n%s\n' "evidence: $EVIDENCE"
