# Separate Wayland EGL runtime for LinPad

The physical iPad on LinPad build 4445 now passes software EGL display initialization and both external SDL2 window-creation tests. The diagnostic game wrapper uses that auxiliary software EGL setup while explicitly selecting Mesa26 Venus for Vulkan. Hollow Knight rendering, playability and FPS remain unverified by these tests.

This package adds `/opt/hollow-egl` while preserving `/opt/hollow-runtime` and `/opt/hollow-mesa26`. It provides the official Debian trixie GLVND EGL router and a consistent Mesa25 EGL/Gallium/DRI stack, plus the small Intel DRM, PCI access and sensors dependencies required by Gallium. Matching GBM25, LLVM19 and other base libraries are reused. The actual ELF dependency closure resolves across 42 binaries. Mesa26 Gallium requires GLIBC_2.43, beyond the installed GLIBC_2.41, so that candidate is excluded.

Run `install.sh` from the unpacked payload archive. It verifies base libc/GBM/LLVM/GLdispatch and Venus fingerprints, checks each payload file/link, refuses conflicting files, and writes only under `/opt/hollow-egl`. The installation passed on the physical device; `egl-device-install.txt` records the accepted checksums and `EGL_OVERLAY_DEVICE_INSTALL_PASS`. The 13.9 MB installed archive remains unchanged. Removing the new prefix when unused restores the previous package state.

## Physical validation

| Check | Result | Evidence |
| --- | --- | --- |
| Zink EGL | Libraries/symbols and Wayland connection pass; timed out during `eglInitialize` at 90 seconds, exit 142 | `egl4445-zink-timeout.txt` |
| Software EGL | EGL 1.5 initialized, 16 RGB8 OpenGL and 16 ES2 window configurations; terminated normally in 31.163 seconds, exit 0 | `egl4445-software-pass.txt` |
| SDL2 Vulkan window | 640×360 window creation, events and cleanup pass in 7.752 seconds, exit 0 | `sdl-window4445-software-results.txt` |
| First SDL2 OpenGL attempt | 15-second diagnostic deadline expired; shorter than measured EGL startup, so insufficient allowance | `sdl-window4445-software-results.txt` |
| SDL2 OpenGL with 60-second deadline | 640×360 window creation, events and cleanup pass in 53.800 seconds, exit 0 | `sdl-window4445-software-gl60-result.txt` |

The EGL probe creates no context or window. The SDL2 probe uses external SDL 2.32.4 and creates windows without GL contexts, Vulkan surfaces or rendered frames. These results do not establish Unity's embedded SDL behavior or game performance. Source, exact probe binaries and wrapper scripts are archived; the initial 15-second SDL binary/source are retained separately. `verification.json` records these gates and `provenance.json` binds the artifacts by SHA256.

## Separate diagnostic game wrapper

`fex-hollow-egl` is a translator hook for the existing `launch-game`. Install it as `/tmp/fex-hollow-egl`, make it executable, and set `LINPAD_FEX_BINARY=/tmp/fex-hollow-egl` and `LINPAD_FEX_DISPLAY_MODE=wayland` for a diagnostic launch. Keep the engine's `-force-wayland -force-vulkan` flags. `hollow4445-egl-software.sh` records the exact parent-operated game command, including `-timestamps`, 960×540 and windowed mode; its outcome remains a separate gate. The production Steam tool was not changed by this wrapper.

The hook runs after the Steam launcher cleans its environment. Default `LINPAD_EGL_MODE=swrast` sets `MESA_LOADER_DRIVER_OVERRIDE=swrast`, `GALLIUM_DRIVER=llvmpipe` and `LIBGL_ALWAYS_SOFTWARE=1` for auxiliary EGL. Optional `LINPAD_EGL_MODE=zink` selects the still-unqualified Zink route. It orders EGL25, Mesa26 and base library paths, selects the EGL25 vendor JSON, and leaves the explicit Mesa26 Venus ICD in place. `VK_DRIVER_FILES`, inherited Vulkan layers and `LD_PRELOAD` are cleared. It invokes `/opt/fex/FEX-apple-m1-munmap` and logs only the selected runtime/display paths and mode. It preserves cwd and all arguments; `-timestamps` passes unchanged, or `LINPAD_EGL_ADD_TIMESTAMPS=1` adds it once.

Diagnostic overrides are `LINPAD_EGL_FEX_BINARY`, `LINPAD_EGL_PREFIX`, `LINPAD_EGL_MESA_PREFIX`, and the existing `LINPAD_FEX_RUNTIME`, `LINPAD_FEX_ROOTFS`, `LINPAD_FEX_ICD`, `LINPAD_FEX_EXTRA_LIBRARY_PATH`, `LINPAD_FEX_XDG_RUNTIME_DIR` and `LINPAD_FEX_WAYLAND_DISPLAY`. The default Wayland socket is `/tmp/xdg-runtime/wayland-0-gpu`.

Host-only integration passed 50 checks through the actual launcher, proving argv/cwd retention, timestamp handling and environment sanitation/restore with a mock FEX. Reproduce from this directory with `python3 validate-egl-launcher.py --launcher launch-game-tested`; this does not execute a game or touch a device. The wrapper shell syntax also passes. `EGL-VULKAN-AUDIT.md` explains the ELF/runtime separation and the additional actual-game GPU/maps evidence still required.
