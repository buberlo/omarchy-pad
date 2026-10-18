#!/bin/sh
# The race the writer's retry exists for: a request arrives before the supervisor's reader
# is attached (start-up, or the moment between two writers). And the failure case: no
# supervisor at all must fail in bounded time instead of hanging.
set -u
rm -f /run/omarchy-pad/launch /work/raced.out /work/nosupervisor.out
echo "== a) request sent 1s before the supervisor exists"
( sleep 1; sh /work/mock-init.sh >/dev/null 2>&1 ) &
start=$(date +%s)
/work/spawn-writer 'sh -c "echo raced > /work/raced.out"'
echo "writer exit=$? after $(( $(date +%s) - start ))s"
sleep 1
cat /work/raced.out 2>/dev/null || echo "(raced.out missing)"
echo "== supervisor log"
cat /run/omarchy-pad/init.log
pkill -f mock-init.sh 2>/dev/null
sleep 0.5
echo "== b) request with no supervisor at all"
rm -f /run/omarchy-pad/launch
start=$(date +%s)
/work/spawn-writer 'sh -c "echo nosupervisor > /work/nosupervisor.out"'
echo "writer exit=$? after $(( $(date +%s) - start ))s"
ls /work/nosupervisor.out 2>/dev/null || echo "(nosupervisor.out missing, as expected)"
