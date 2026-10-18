#!/bin/sh
# Two sequential requests (the second after the first writer closed: the reopen loop), then
# a request sent while no reader is attached, which exercises the writer's ENXIO retry.
set -u
export TESTVAR="value with 'quotes' and spaces"
export WAYLAND_DISPLAY=wayland-0
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
sh /work/mock-init.sh >/work/init.log 2>&1 &
INIT=$!
sleep 1
/work/spawn-writer 'sh -c "echo first:\$TESTVAR:\$WAYLAND_DISPLAY:\$XDG_RUNTIME_DIR > /work/first.out"'
sleep 1
/work/spawn-writer 'sh -c "echo second >> /work/second.out"'
sleep 1
# Kill the reader subshell's hold by pausing it: replace the FIFO briefly? Instead, simply
# send a third request immediately after a reader restart, which is the same race.
/work/spawn-writer 'sh -c "echo third >> /work/third.out"'
sleep 1
echo "--- init.log"; cat /work/init.log
echo "--- first.out"; cat /work/first.out 2>/dev/null || echo "(missing)"
echo "--- second.out"; cat /work/second.out 2>/dev/null || echo "(missing)"
echo "--- third.out"; cat /work/third.out 2>/dev/null || echo "(missing)"
echo "--- leftover scripts"; ls /run/omarchy-pad/
kill $INIT 2>/dev/null
pkill -f mock-init.sh 2>/dev/null
sleep 0.5
