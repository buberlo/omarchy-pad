#!/bin/sh
# 03: start the key watcher and the game, both detached, so this command returns at once and
# the agent stays free. The game reaches its menu after roughly half a minute.
set -u
SERVER=http://192.168.0.104:8766

if [ ! -s /tmp/kb4453-watch ]; then
    wget -T 20 -qO /tmp/kb4453-watch "$SERVER/kb4453-watch" || echo "WATCHER_DOWNLOAD_FAILED=1"
fi
if [ -f /tmp/kb4453-watch.pid ] && kill -0 "$(cat /tmp/kb4453-watch.pid)" 2>/dev/null; then
    echo "watcher already running: $(cat /tmp/kb4453-watch.pid)"
else
    setsid sh /tmp/kb4453-watch >/dev/null 2>&1 &
    sleep 2
    echo "watcher pid: $(cat /tmp/kb4453-watch.pid 2>/dev/null)"
fi

if [ -f /tmp/hollow4452-egl ]; then
    if [ -f /tmp/kb4453-game.pid ] && kill -0 "$(cat /tmp/kb4453-game.pid)" 2>/dev/null; then
        echo "the game launcher is already running: $(cat /tmp/kb4453-game.pid)"
    else
        setsid sh -c 'echo $$ > /tmp/kb4453-game.pid; exec sh /tmp/hollow4452-egl' \
            > /tmp/hollow4452-egl-run.log 2>&1 &
        sleep 8
        echo "game launcher pid: $(cat /tmp/kb4453-game.pid 2>/dev/null)"
    fi
else
    echo "NO_LAUNCHER (/tmp/hollow4452-egl is missing)"
fi

echo
echo "=== processes ==="
ps -o pid,comm 2>/dev/null | grep -iE "hollow|FEX|ishwl" | head -6
echo
echo "=== unity log so far ==="
tail -6 /tmp/hollow4452-egl-unity.txt 2>&1
echo
echo "=== compositor frames ==="
wc -l /tmp/hollow4452-frames.txt 2>&1
