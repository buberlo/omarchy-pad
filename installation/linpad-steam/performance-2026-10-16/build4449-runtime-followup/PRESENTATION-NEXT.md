# Narrow next presentation observation

Current compositor observables are passive: check the known ishwl log and
frame file, then identify a map with the exact game title and a live matching
vN-generation.buf file. Do not consume bridge events/notify FIFOs. The game
can keep running during these checks. Native Terminal/DesktopKit content does
not imply a Linux Wayland map record exists.

The source path is wl-bridge/src/xdg.c (map state after a committed nonempty
surface buffer) and view.c composite_view (frame row after composition and
before bridge delivery/acknowledgement). Zero rows alone cannot distinguish
no image submission, invalid geometry, pending callbacks or absent logging.
Combining the known log start with no game map narrows the boundary but still
does not identify a Vulkan call or return value.

If the same launch continues without an image, a separate diagnostic game
launch can observe vkCreateSwapchainKHR, vkAcquireNextImageKHR/2KHR,
vkQueueSubmit/2/KHR, vkQueuePresentKHR and waits. Record first entry and return,
VkResult and device/queue/swapchain identity, then bounded periodic counts.
Keep entry and return separate to expose a call that never returns. Report
successful presents as API progress only; they do not prove display delivery.

Do not reuse the earlier vulkan-present-timing.c cube proxy. It has global
single-device/queue state, reports only at exit and was not qualified for Unity.
A new candidate needs real-loader global dispatch trampolines or proper per-
device dispatch, synchronization, pass-through of extension availability,
multi-device/queue tests, direct and get-proc-address lookup coverage, and a
bounded log. Keep it in a separate optional loader directory and never replace
the base Vulkan loader or production launcher. Test locally before any physical
use, then use only the parent-owned device workflow.

A next-run WAYLAND_DEBUG=client capture is a lower-implementation alternative
to locate wl_surface attach/commit/frame events and responses. It cannot by
itself distinguish time spent before Vulkan present or inside GPU/fence waits,
and protocol logging changes runtime overhead. No restart or new hook was
performed by the archiving agent.
