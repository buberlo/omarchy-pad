#!/bin/sh
cd '/root/.local/share/Steam/steamapps/common/Hollow Knight' || exit 80
export LINPAD_FEX_DISPLAY_MODE=wayland
export SteamAppId=367520 SteamGameId=367520 LINPAD_FEX_BINARY=/tmp/fex-hollow-egl LINPAD_EGL_MODE=swrast
unset WAYLAND_DEBUG EGL_LOG_LEVEL LIBGL_DEBUG FEX_MULTIBLOCK
export FEX_HOSTFEATURES=disablelrcpc2
export LINPAD_EGL_FEX_BINARY=/tmp/fex-game-crash-report LINPAD_FEX_REAL_BINARY=/opt/fex/FEX-apple-m1-munmap
OUT=/tmp/hollow4451-rcpc2-fex.txt
{
 date
 /root/.local/share/Steam/compatibilitytools.d/linpad-fex-native/run run -- ./hollow_knight.x86_64 -timestamps -force-wayland -force-vulkan -screen-width 960 -screen-height 540 -screen-fullscreen 0 -logFile /tmp/hollow4451-rcpc2-unity.txt
 echo GAME_EXIT=$?
 echo UNITY_LOG
 cat /tmp/hollow4451-rcpc2-unity.txt
 date
} > "$OUT" 2>&1
wget -T 8 -q --post-file="$OUT" http://192.168.0.104:8766/report -O /dev/null
tail -30 "$OUT"
