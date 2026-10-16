LinPad 1.4.0 build 4447 fixes incorrect `/proc/self/maps` boundaries that caused glibc to infer a false main-stack range and Mono to protect live FEX metadata.

Built and signed from `9beb5dea2eeaffd7ebd27952592cba658fe12bb4`; signed IPA and matching dSYM passed CRC/content verification, matching UUID `DEDA2762-5AAF-3685-9513-27727BEF64C6`. Both approved memory entitlements remain enabled. Installed and booted on the physical iPad with 512 MiB native JIT; physical native map-boundary regression passes 10/10; physical default-FEX Mono initialization passes with no SDL and with the real Wayland SDL backend.

Game rendering, playability and steady-state performance remain pending. Installed FEX/Mesa/EGL/Steam/game data are separate from the IPA. This is a development checkpoint.
