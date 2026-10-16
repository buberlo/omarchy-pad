#!/bin/sh
OUT=/tmp/hollow4445-egl-software-status.txt
{
 date
 echo GAME_OUTPUT
 tail -70 /tmp/hollow4445-egl-software-fex.txt
 echo UNITY_LOG
 cat /tmp/hollow4445-egl-software-unity.txt
 echo PROCESSES
 ps -o pid,ppid,stat,comm | grep -E 'hollow|FEX|Xwayland|steam'
 for gamepid in $(ps -o pid,comm | awk '/FEX-apple-m1-mu/ {print $1}'); do
  echo GAME_PID=$gamepid
  echo GAME_STAT
  cat /proc/$gamepid/stat
  echo GAME_GRAPHICS_MAPS
  grep -E 'libvulkan_virtio|libvulkan\.so|libEGL_mesa|libgallium|libLLVM|libgbm' /proc/$gamepid/maps
 done
 echo MEMORY
 cat /proc/ish/memory
} > "$OUT" 2>&1
wget -T 8 -q --post-file="$OUT" http://192.168.0.104:8766/report -O /dev/null
tail -20 "$OUT"
