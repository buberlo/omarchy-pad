#!/bin/sh
set -eu
curl -fL --max-time 120 http://192.168.0.104:8765/steam-gtk.tar.gz -o /tmp/steam-gtk.tar.gz
tar -xzf /tmp/steam-gtk.tar.gz -C /opt/steam-host
B="$HOME/.local/share/Steam/steamrtarm64"
L=/opt/steam-host/usr/lib/aarch64-linux-gnu
"$L/ld-linux-aarch64.so.1" --library-path "$L:$L/pulseaudio:$B:$B/libs" --list "$B/steamui.so" > /tmp/steam-ui-libs.log 2>&1 || true
cat /tmp/steam-ui-libs.log
wget -q --post-file=/tmp/steam-ui-libs.log http://192.168.0.104:8765/reports/steamwebhelper.log -O /dev/null
