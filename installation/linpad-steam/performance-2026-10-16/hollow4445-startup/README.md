# Build 4445: Hollow Knight startup evidence

This records a partial physical-device startup, not gameplay. Hollow Knight's native Linux depot is installed. LinPad build 4445 boot and protected-memory checks pass, and the locally rebuilt FEX allocator passes all 18 low-address munmap checks on this iPad without a preload shim. See [FEX repair](../fex-munmap-repair/README.md) and [build verification](../build4445/VERIFICATION.md).

The direct game run began at 10:08:19 UTC on 16 October 2026, using the patched FEX runtime, the existing Vulkan configuration and no forced SDL video driver. Unity 6000.0.61f1 progressed through Mono/input/physics startup and selected `Virtio-GPU Venus (Apple M2 GPU)` while explicitly forcing Vulkan. Its complete captured log also reports `Error creating MainPlayerWindow: Failed loading libGL.so.1` because that library is absent. The excerpt preserves both facts. The earlier `(null)` window-backend message alone does not establish a permanently failed initialization.

The operator manually stopped this run at 10:11:47 UTC while it was still making progress, to prepare a controlled follow-up. The resulting `Killed` and `GAME_EXIT=137` are a manual termination, not a spontaneous crash or proof of a hang. There is no successful game window, menu, gameplay, input, audio or sustained performance result from this run. A minimal official libGL dependency repair is being prepared separately; this record does not claim it has been installed or fixed the game.

The preceding independent x86 SDL2 probe opened the Xwayland display and initialized the X11 video backend successfully: one display, 659×540. That narrows the failure but does not prove Unity's embedded window layer has all its dependencies. The probe uses a different SDL library from Unity's embedded implementation.

The excerpts contain unchanged selected log lines, with omitted line ranges recorded in `PROVENANCE.json`. The complete original reports remain in the local workspace and are identified by byte count and SHA-256. No authentication or preferences files are included.
