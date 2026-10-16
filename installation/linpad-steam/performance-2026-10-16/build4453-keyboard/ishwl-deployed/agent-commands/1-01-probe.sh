#!/bin/sh
# 01: is the agent alive, and what does the device look like before the next step?
set -u
date -u
echo "AGENT_PID=$(cat /tmp/kb-agent.pid 2>/dev/null)"
echo "AGENT_LOG=$(tail -1 /tmp/kb-agent.log 2>/dev/null)"

echo
echo "=== compositor ==="
ls -l /usr/local/bin/ishwl 2>&1
sha256sum /usr/local/bin/ishwl 2>&1
echo "backup: $(sha256sum /tmp/ishwl-before-kb4453 2>&1)"

echo
echo "=== session log ==="
ls -l /tmp/ishwl.log 2>&1
echo "first lines:"
head -3 /tmp/ishwl.log 2>&1
echo "key lines: $(grep -cE '^ishwl [0-9.]+: key ' /tmp/ishwl.log 2>/dev/null)"

echo
echo "=== frame hook and log ==="
cat /etc/ishwl/session.d/99-kb4453-frames.sh 2>&1
ls -l /tmp/hollow4452-frames.txt 2>&1
echo "frame rows: $(wc -l < /tmp/hollow4452-frames.txt 2>/dev/null)"
echo "session hooks:"
ls -l /etc/ishwl/session.d/ 2>&1

echo
echo "=== game processes ==="
ps -o pid,comm 2>/dev/null | grep -iE "hollow|FEX|ishwl" | head -6

echo
echo "=== load and memory ==="
cat /proc/loadavg 2>/dev/null
head -6 /proc/ish/memory 2>/dev/null
