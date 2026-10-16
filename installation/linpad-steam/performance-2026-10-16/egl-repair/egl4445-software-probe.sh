#!/bin/sh
# Diagnostic only. Install EGL payload and /tmp/egl-display-probe first.
egl=/opt/hollow-egl
mesa=/opt/hollow-mesa26
base=/opt/hollow-runtime
OUT=/tmp/egl4445-software-probe.txt
{
    date
    unset LD_PRELOAD LD_AUDIT LD_DEBUG LIBGL_ALWAYS_SOFTWARE LIBGL_ALWAYS_INDIRECT
    unset GALLIUM_DRIVER MESA_GL_VERSION_OVERRIDE MESA_GLES_VERSION_OVERRIDE
    unset DISPLAY XAUTHORITY VK_INSTANCE_LAYERS VK_LAYER_PATH VK_ADD_LAYER_PATH
    export XDG_RUNTIME_DIR=/tmp/xdg-runtime WAYLAND_DISPLAY=wayland-0-gpu
    export FEX_ROOTFS="$base" FEX_DISABLETELEMETRY=1
    export LD_LIBRARY_PATH="$egl/usr/lib/x86_64-linux-gnu:$mesa/usr/lib/x86_64-linux-gnu:$base/usr/lib/x86_64-linux-gnu:$base/usr/lib/x86_64-linux-gnu/pulseaudio"
    export __EGL_VENDOR_LIBRARY_FILENAMES="$egl/usr/share/glvnd/egl_vendor.d/50_mesa.json"
    export MESA_LOADER_DRIVER_OVERRIDE=swrast
    export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
    export LIBGL_DRIVERS_PATH="$egl/usr/lib/x86_64-linux-gnu/dri"
    export VK_ICD_FILENAMES="$mesa/usr/share/vulkan/icd.d/virtio_icd.json"
    export VK_LOADER_LAYERS_DISABLE='*' MESA_VK_WSI_DEBUG=sw EGL_LOG_LEVEL=debug
    timeout -k 3 105 /opt/fex/FEX-apple-m1-munmap /tmp/egl-display-probe
    result=$?
    echo EGL_PROBE_EXIT=$result
    date
} > "$OUT" 2>&1
wget -T 8 -q --post-file="$OUT" http://192.168.0.104:8766/report -O /dev/null
cat "$OUT"
