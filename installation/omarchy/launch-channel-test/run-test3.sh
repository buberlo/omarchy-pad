#!/bin/sh
# a) ENOENT race: the channel does not exist yet and the supervisor appears inside the
#    writer's 500ms budget -> delivered.
# b) ENXIO race: the FIFO exists but no reader is attached yet (the gap between two writers)
#    and the supervisor attaches inside the 5s budget -> delivered.
set -u
RUNDIR=/run/omarchy-pad
mkdir -p "$RUNDIR"
echo "== a) supervisor starts 200ms after the request"
rm -f "$RUNDIR/launch" /work/a.out
( sleep 0.2; sh /work/mock-init.sh >/dev/null 2>&1 ) &
/work/spawn-writer 'sh -c "echo a-ok > /work/a.out"'
echo "writer exit=$?"
sleep 1
cat /work/a.out 2>/dev/null || echo "(a.out missing)"
pkill -f mock-init.sh; sleep 0.5

echo "== b) FIFO exists, reader attaches 500ms later"
rm -f "$RUNDIR/launch" /work/b.out
mkfifo "$RUNDIR/launch"
( sleep 0.5; sh /work/mock-init.sh >/dev/null 2>&1 ) &
start=$(date +%s)
/work/spawn-writer 'sh -c "echo b-ok > /work/b.out"'
echo "writer exit=$? after $(( $(date +%s) - start ))s"
sleep 1
cat /work/b.out 2>/dev/null || echo "(b.out missing)"
echo "== supervisor log"; cat "$RUNDIR/init.log"
pkill -f mock-init.sh
echo "== leftovers"; ls "$RUNDIR"
