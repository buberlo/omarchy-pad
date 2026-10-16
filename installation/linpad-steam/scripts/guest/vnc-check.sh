#!/bin/sh
set -eu
if [ ! -x /opt/steam-host/usr/bin/Xtigervnc ]; then
    curl -fL --max-time 180 http://192.168.0.104:8765/steam-vnc.tar.gz -o /tmp/steam-vnc.tar.gz
    tar -xzf /tmp/steam-vnc.tar.gz -C /opt/steam-host
fi
L=/opt/steam-host/usr/lib/aarch64-linux-gnu
export LD_LIBRARY_PATH="$L:$L/pulseaudio"
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe MESA_LOADER_DRIVER_OVERRIDE=swrast
export LIBGL_DRIVERS_PATH="$L/dri" __GLX_VENDOR_LIBRARY_NAME=mesa
export XKB_CONFIG_ROOT=/opt/steam-x11/usr/share/X11/xkb
export PATH="/opt/steam-x11/usr/bin:$PATH"
"$L/ld-linux-aarch64.so.1" --library-path "$LD_LIBRARY_PATH" /opt/steam-host/usr/bin/Xtigervnc :55 -geometry 1024x700 -depth 24 -SecurityTypes None -localhost yes -rfbport 5955 -nolisten tcp -ac >/tmp/steam-xvnc.log 2>&1 &
server=$!
trap 'kill "$server" 2>/dev/null || true' EXIT INT TERM
sleep 3
export DISPLAY=:55
"$L/ld-linux-aarch64.so.1" --library-path "$LD_LIBRARY_PATH" /opt/steam-host/usr/bin/glxinfo.aarch64-linux-gnu -B >/tmp/glx-vnc.log 2>&1 || true
cat /tmp/steam-xvnc.log /tmp/glx-vnc.log
cat /tmp/steam-xvnc.log /tmp/glx-vnc.log > /tmp/vnc-combined.log
wget -q --post-file=/tmp/vnc-combined.log http://192.168.0.104:8765/reports/console-linux.txt -O /dev/null
