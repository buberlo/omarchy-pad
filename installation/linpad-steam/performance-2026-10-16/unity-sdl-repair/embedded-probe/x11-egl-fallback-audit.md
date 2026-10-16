# X11 software EGL bootstrap fallback

Read-only compatibility audit, 2026-10-16. No device operations, launcher changes or default changes.

`SDL_VIDEO_X11_FORCE_EGL=1` is an official SDL2 hint selecting EGL instead of GLX. It must be present before SDL's X11 device is created. [SDL hint reference](https://wiki.libsdl.org/SDL2/SDL_HINT_VIDEO_X11_FORCE_EGL)

SDL 2.32.4 `X11_CreateDevice` initially sets GLX callbacks, then checks this hint and replaces them with `X11_GLES_*` callbacks before video initialization. Despite the GLES naming, `X11_GLES_LoadLibrary` retains EGL for desktop OpenGL when the hint is set. This bypasses the GLX loader's `glXQueryExtension` failure. Reading only the later hint test inside `X11_GL_LoadLibrary` would miss this earlier dispatch selection. [X11 device initialization, lines 261–288](https://raw.githubusercontent.com/libsdl-org/SDL/release-2.32.4/src/video/x11/SDL_x11video.c), [X11 EGL loader](https://raw.githubusercontent.com/libsdl-org/SDL/release-2.32.4/src/video/x11/SDL_x11opengles.c)

The external SDL2 binary used by the successful Wayland probes is version 2.32.4, SHA256 `ee3ccdd9b5fead3ea6b55622df02b962ef7f0afd6dc193139e5b9c088f56a43f`. Its hint string occurs at file offset `0x186e84`.

Exact embedded support is now verified in the local UnityPlayer binary: 39,255,232 bytes, SHA256 `12ce7af93c41b13b3e7d9b75b591336cc56c9ce113714143fd897cc1dcf00e33`. At `UnityPlayer+0x247b4b2`, the X11 device constructor reads this hint, converts it to a boolean, and replaces the GLX callbacks with EGL callbacks. Its selected load function `+0x2483ccc` keeps EGL when the hint is true and dispatches to `+0x2473e54`, rather than GLX. The disassembly is saved in `unity-window-inspection/xref-06-247b4b2.txt` and `sdl-x11-egl-load.txt`. This establishes code support, not device success.

Exact Mesa 25.0.7 source is saved as `mesa25-platform_x11.c`. Forced software rendering selects `dri2_initialize_x11_swrast` (line 2089). The non-Zink path obtains XCB/display visuals, optionally uses MIT-SHM and builds the software screen; it does not require GLX or enter the Zink-only DRI3 connection path (lines 1823–1902). X11/XCB/SHM libraries already appear in the prepared dependency closure. This supports a bounded X11 EGL window test without adding packages; it does not prove the server's complete EGL or Vulkan presentation path. [Mesa 25.0.7 X11 EGL source](https://gitlab.freedesktop.org/mesa/mesa/-/raw/mesa-25.0.7/src/egl/drivers/dri2/platform_x11.c)

The current `serve/fex-hollow-egl` hook explicitly clears `DISPLAY` and `XAUTHORITY` and forces Wayland. A proposed **separate** X11 diagnostic hook must preserve the fresh `ishwl-x11` display/auth values, select `SDL_VIDEODRIVER=x11`, `EGL_PLATFORM=x11` and `SDL_VIDEO_X11_FORCE_EGL=1`, then apply the same isolated software EGL package and explicit Venus Vulkan ICD. The game must omit `-force-wayland` while retaining `-force-vulkan`. Merely setting `LINPAD_FEX_DISPLAY_MODE=x11` on the current hook is insufficient.

The existing `sdl-window-probe` explicitly calls `SDL_VideoInit("wayland")`; setting an environment variable cannot make it test X11. A separate diagnostic copy selecting `x11` should pass its OpenGL window gate before another long Unity attempt. No such X11 probe or game pass is claimed by this source review.

The next priority changed after inspecting the exact SDL initialization caller: an independently reproduced UDEV failure rolls back video initialization. This affects the shared SDL subsystem setup regardless of X11/Wayland. See `unity-sdl-udev-audit.md`; switching window backends would not repair that input-monitor failure.
