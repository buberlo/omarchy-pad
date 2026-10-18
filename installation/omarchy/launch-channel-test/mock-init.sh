#!/bin/sh
RUNDIR=/run/omarchy-pad
LOG=$RUNDIR/init.log
mkdir -p "$RUNDIR"
: >"$LOG"
log() { printf "%s %s\n" "$(date -u +%H:%M:%S)" "$*" >>"$LOG"; }
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
LAUNCH_FIFO=$RUNDIR/launch
rm -f "$LAUNCH_FIFO"
mkfifo "$LAUNCH_FIFO" 2>/dev/null || log "could not create $LAUNCH_FIFO"
(
    # Every writer closing ends the inner read loop with EOF; the outer loop reopens the
    # FIFO (blocking until the next writer) so the channel survives ishwl restarts and any
    # gap between requests. Without it the first launch would also be the last.
    while :; do
        while IFS= read -r cmd; do
            [ -n "$cmd" ] || continue
            log "launch request: $cmd"
            # cmd is the path of a self-deleting launcher script ishwl wrote; it carries
            # ishwl's environment, which this supervisor does not share.
            setsid sh -c "$cmd" >>"$RUNDIR/launched.log" 2>&1 &
        done <"$LAUNCH_FIFO"
    done
) &
log "launch channel ready at $LAUNCH_FIFO"
while :; do
    wait
done
