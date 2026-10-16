#!/bin/sh
OUT=/tmp/build4443-cube-visible.txt
{
 ulimit -s 8192
 unset LD_PRELOAD LIBGL_ALWAYS_SOFTWARE GALLIUM_DRIVER MESA_LOADER_DRIVER_OVERRIDE
 export FEX_DISABLETELEMETRY=1 FEX_ROOTFS=/opt/hollow-runtime
 export LD_LIBRARY_PATH=/opt/hollow-mesa26/usr/lib/x86_64-linux-gnu:/opt/hollow-runtime/usr/lib/x86_64-linux-gnu
 export XDG_RUNTIME_DIR=/tmp/xdg-runtime WAYLAND_DISPLAY=wayland-0-gpu MESA_VK_WSI_DEBUG=sw
 export VK_LOADER_LAYERS_DISABLE='*'
 export VK_ICD_FILENAMES=/opt/hollow-mesa26/usr/share/vulkan/icd.d/virtio_icd.json
 date
 time timeout -k 3 120 /opt/fex/FEX-apple-m1 /opt/hollow-runtime/usr/bin/vkcube --wsi wayland --width 640 --height 360 --c 1200
 echo CUBE_EXIT=$?
 date
} > "$OUT" 2>&1
unset LD_LIBRARY_PATH
wget -T 8 -q --post-file="$OUT" http://192.168.0.104:8766/report -O /dev/null
tail -12 "$OUT"
