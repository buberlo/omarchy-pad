#!/bin/sh
set -eu
curl -fL --max-time 120 http://192.168.0.104:8765/steam-cef-glibc.tar.gz -o /tmp/steam-cef-glibc.tar.gz
tar -xzf /tmp/steam-cef-glibc.tar.gz -C /opt/steam-host
L=/opt/steam-host/usr/lib/aarch64-linux-gnu
B="$HOME/.local/share/Steam/steamrtarm64"
"$L/ld-linux-aarch64.so.1" --library-path "$L:$B:$B/libs" --list "$B/steamwebhelper" >/tmp/steamwebhelper.log 2>&1 || true
cat /tmp/steamwebhelper.log
wget -q --post-file=/tmp/steamwebhelper.log http://192.168.0.104:8765/reports/steamwebhelper.log -O /dev/null
