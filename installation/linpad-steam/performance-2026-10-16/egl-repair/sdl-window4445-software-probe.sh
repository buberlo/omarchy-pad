#!/bin/sh
# Run only after egl4445-probe passes and /tmp/sdl-window-probe is installed.
egl=/opt/hollow-egl
mesa=/opt/hollow-mesa26
base=/opt/hollow-runtime
OUT=/tmp/sdl-window4445-software-probe.txt
{
    date
    unset LD_PRELOAD LD_AUDIT LD_DEBUG LIBGL_ALWAYS_SOFTWARE LIBGL_ALWAYS_INDIRECT
    unset GALLIUM_DRIVER MESA_GL_VERSION_OVERRIDE MESA_GLES_VERSION_OVERRIDE
    unset DISPLAY XAUTHORITY VK_INSTANCE_LAYERS VK_LAYER_PATH VK_ADD_LAYER_PATH
    unset SDL_DYNAMIC_API SDL3_DYNAMIC_API SDL_VIDEODRIVER SDL_VIDEO_DRIVER
    export XDG_RUNTIME_DIR=/tmp/xdg-runtime WAYLAND_DISPLAY=wayland-0-gpu
    export FEX_ROOTFS="$base" FEX_DISABLETELEMETRY=1
    export LD_LIBRARY_PATH="$egl/usr/lib/x86_64-linux-gnu:$mesa/usr/lib/x86_64-linux-gnu:$base/usr/lib/x86_64-linux-gnu:$base/usr/lib/x86_64-linux-gnu/pulseaudio"
    export __EGL_VENDOR_LIBRARY_FILENAMES="$egl/usr/share/glvnd/egl_vendor.d/50_mesa.json"
    export MESA_LOADER_DRIVER_OVERRIDE=swrast
    export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
    export LIBGL_DRIVERS_PATH="$egl/usr/lib/x86_64-linux-gnu/dri"
    export VK_ICD_FILENAMES="$mesa/usr/share/vulkan/icd.d/virtio_icd.json"
    export VK_LOADER_LAYERS_DISABLE='*' MESA_VK_WSI_DEBUG=sw EGL_LOG_LEVEL=debug
    for mode in vulkan opengl; do
        echo SDL_MODE_BEGIN=$mode
        timeout -k 3 105 /opt/fex/FEX-apple-m1-munmap /tmp/sdl-window-probe "$mode"
        result=$?
        echo SDL_MODE_EXIT=$mode:$result
        date
    done
} > "$OUT" 2>&1
wget -T 8 -q --post-file="$OUT" http://192.168.0.104:8766/report -O /dev/null
cat "$OUT"
