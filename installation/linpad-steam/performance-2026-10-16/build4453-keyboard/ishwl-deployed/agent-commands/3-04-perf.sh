#!/bin/sh
# 04: the bounded gameplay measurement (30 s frame count, process and memory state, Pulse
# stream) through the archived helpers. Takes about a minute; the agent is blocked for that
# long, which is fine.
set -u
SERVER=http://192.168.0.104:8766
if [ ! -s /tmp/kb4453-perf ]; then
    wget -T 20 -qO /tmp/kb4453-perf "$SERVER/kb4453-perf" || { echo "PERF_DOWNLOAD_FAILED=1"; exit 1; }
fi
sh /tmp/kb4453-perf
