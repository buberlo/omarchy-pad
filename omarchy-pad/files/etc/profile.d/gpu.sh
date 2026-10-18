#!/bin/sh
# Graphics defaults for every shell in the Omarchy Pad guest.
#
# The emulator offers one DRM node -- /dev/dri/renderD128, an emulated virtio-gpu whose
# Vulkan is Mesa's Venus on MoltenVK. Without these three variables an application either
# finds no Vulkan driver or picks a WSI it cannot present; with them GL and GLES go through
# zink on Venus and present through shared memory, which is what ishwl and the display
# bridge consume. The session adds MESA_GLES_VERSION_OVERRIDE=3.0 on top, because the
# compositor needs a 3.0 context.
#
# The hook is deliberately silent when there is no render node (an old emulator build):
# nothing here is required for a shell to work.
if [ ! -c /dev/dri/renderD128 ]; then
    return 0 2>/dev/null || exit 0
fi

export VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/virtio_icd.aarch64.json}
export MESA_LOADER_DRIVER_OVERRIDE=${MESA_LOADER_DRIVER_OVERRIDE:-zink}
export MESA_VK_WSI_DEBUG=${MESA_VK_WSI_DEBUG:-sw}
