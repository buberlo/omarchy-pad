# Build4449 runtime follow-up snapshot

This snapshot is separate from the frozen signed-build/probe checkpoint. The
game launch record is 12:47:41 UTC. Vulkan API initialization is logged at
12:47:47.661 (about 6.7 seconds after launch). Mono assembly reload spans
12:47:48.632–12:47:54.098 and reports 5.466 seconds; this is the reload duration,
not the elapsed time since game launch. Unity enters InitializeOrResetSwapChain
for 960×540 at 12:48:07.963, about 27 seconds after launch. These are individual
startup observations, not a controlled repeated benchmark or time to first frame.

The bounded CPU capture runs from 12:48:35.823 to 12:48:41.613 UTC, during the
Steam initialization part of startup. Across 6,202 samples / 6.202 seconds of
sample weight, ss_get self weight is 0.133 seconds (2.14%). The preceding
build4448 startup capture had 83.58% ss_get self weight and its later capture
had 33.30%. Different startup phases and cache/thermal conditions prevent an
overall speedup estimate. The new largest individual self entry is memmove
at 15.82%; no further implementation change is justified by this summary alone.

Managed startup continues through unload, language restoration and object
cleanup. Steam initialization fails because sdk64/steamclient.so is missing.
The log also reports missing Game Manager, GameCameras and UIManager objects.
These messages are retained as observations without asserting that any one
causes the missing image. No FMOD initialization error appears in the captured
startup report; that is not an audio-playback qualification.

At 12:49:25 and 12:50:25 UTC the passive report finds zero frame-log rows and
no compositor map records. No rendered game image or gameplay is established.
InitializeOrResetSwapChain is a Unity stage message, not evidence that
vkCreateSwapchainKHR returned success, vkQueuePresentKHR was called, or a
nonempty buffer reached the compositor. The next diagnostic should distinguish
those boundaries before changing graphics settings.

The two prepared passive scripts use build4449 paths and do not restart the
game. Their collector/parser ancestry and synthetic validation are archived in
build4448-runtime-followup. No fixed-duration selected-view capture was possible
at this snapshot because no game view was identified. Raw traces, device IDs,
process inventories, proprietary inputs and full logs remain outside Git.
