#!/bin/sh
set -eu
BASE="$HOME/.local/share/Steam"
mkdir -p "$BASE/package"
curl -fL --max-time 180 http://192.168.0.104:8765/steam-client.tar.gz -o /tmp/steam-client.tar.gz
tar -xzf /tmp/steam-client.tar.gz -C "$BASE"
printf 'publicbeta\n' > "$BASE/package/beta"
LOADER=/opt/steam-host/usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1
LIBS=/opt/steam-host/usr/lib/aarch64-linux-gnu
"$LOADER" --library-path "$LIBS:$BASE/steamrtarm64" --list "$BASE/steamrtarm64/steam"
printf 'STEAM_ARM64_FILES_INSTALLED\n'
