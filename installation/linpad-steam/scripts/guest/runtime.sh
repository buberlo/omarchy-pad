#!/bin/sh
set -eu
mkdir -p /opt/steam-host
curl -fL --max-time 90 http://192.168.0.104:8765/steam-glibc.tar.gz -o /tmp/steam-glibc.tar.gz
tar -xzf /tmp/steam-glibc.tar.gz -C /opt/steam-host
LOADER=/opt/steam-host/usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1
[ -f "$LOADER" ]
if [ ! -e /lib/ld-linux-aarch64.so.1 ]; then ln -s "$LOADER" /lib/ld-linux-aarch64.so.1; fi
printf 'STEAM_NATIVE_RUNTIME_READY\n'
