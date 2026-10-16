# Unity 6000.0.61f1 Wayland and Vulkan startup

Primary-source review, 2026-10-16. No device interaction or production source changes were performed for this audit.

Unity 6000's Player reference defines `-force-wayland` as an experimental Linux window-system option and `-force-vulkan` as the rendering API option. It lists no conflict between them. This supports testing the combination; it does not establish compatibility with LinPad's compositor or translated graphics stack. [Player arguments](https://docs.unity.com/en-us/engine/6000.0/manual/unity-editor/command-line-arguments/player)

Unity staff confirms SDL handles internal window creation in the official Wayland support discussion. The same discussion contains older user reports of Unity 6.0.14 working with either graphics API under native Wayland; those reports are not qualification of this game's 6000.0.61f1 runtime. [Official discussion, post 21](https://discussions.unity.com/t/experimental-wayland-support-for-linux-player/905188?page=2)

In upstream SDL 2.32.4, `SDL_CreateWindow` loads GL for `SDL_WINDOW_OPENGL` and Vulkan for `SDL_WINDOW_VULKAN`; the flags cannot be combined on one window. Linux does not acquire a default OpenGL flag in `SDL_DefaultGraphicsBackends`. Therefore a Vulkan window alone does not universally require EGL. These are the external probe's source semantics, not proof of Unity's embedded SDL revision. [SDL window creation, lines 1584–1612; default graphics flags, lines 201–214](https://raw.githubusercontent.com/libsdl-org/SDL/release-2.32.4/src/video/SDL_video.c)

SDL's Wayland GL-library path initializes EGL using the native Wayland display. SDL 2.32.4 passes platform 0; the current SDL2 branch explicitly passes the Wayland platform constant. The generic EGL loader obtains GL/GLES and EGL libraries, resolves required EGL functions, obtains a display and initializes it. The exact observed error, `Could not load EGL library`, identifies library loading or missing `eglChooseConfig` before display initialization. [Wayland GL loader, release 2.32.4](https://raw.githubusercontent.com/libsdl-org/SDL/release-2.32.4/src/video/wayland/SDL_waylandopengles.c), [current SDL2 EGL loader, lines 313–404](https://raw.githubusercontent.com/libsdl-org/SDL/SDL2/src/video/SDL_egl.c)

The physical game logs independently show MainPlayerWindow GL/GLX/EGL errors before explicit Vulkan-device selection. The supported inference is that this Unity startup uses an additional GL-related window or bootstrap operation. Its exact caller and flags remain unknown without Unity source or a call trace. Do not state that all Vulkan players require EGL, or that an OpenGL window proves the final game renderer is OpenGL.

The prerequisite now established locally and on device is a usable GL/EGL library closure and working Wayland window setup. The isolated Mesa 25 EGL/Gallium package with software EGL passes display initialization/configuration; external SDL2 2.32.4 passes separate Vulkan and OpenGL window creation/cleanup. The OpenGL probe creates no GL context and the Vulkan probe creates no Vulkan surface. The software path is therefore an auxiliary bootstrap experiment while the explicit Mesa 26 Venus ICD remains selected for the actual Vulkan renderer.

Physical evidence, executed by the parent agent:

- `../reports/egl4445-software-pass.txt`: EGL initialization and RGB8 OpenGL/ES2 window configs pass; cleanup completes in 31.163 seconds.
- `../reports/sdl-window4445-software-results.txt`: Vulkan window setup/cleanup passes in 7.752 seconds. Its original OpenGL 15-second deadline was too short for measured EGL startup.
- `../reports/sdl-window4445-software-gl60-result.txt`: corrected OpenGL deadline; creation succeeds by 40.503 seconds, complete cleanup by 53.800 seconds, exit 0.
- `../reports/build4445-patched-cube.txt`: separate Venus Vulkan rendering test, 1,200 frames with exit 0 and physical visible motion, before this mixed EGL/Unity experiment.

Keep the current controlled game test: native Wayland, software EGL prerequisites, explicit Venus ICD, `MESA_VK_WSI_DEBUG=sw` for the compositor's shared-memory presentation, and `-force-vulkan`. Preserve timestamped milestones and require actual Unity window/rendering evidence before performance tuning. Neither window probe establishes gameplay or frame rate. Zink's separate 90-second EGL-initialization timeout remains unresolved; software EGL avoids that path for this experiment.

The exact runtime release is 6000.0.61f1, changeset `74a0adb02c31`. Its listed fixes/known issues do not identify the observed EGL bootstrap failure. This is a narrow release-note check, not proof that no related Unity defect exists. [Release notes](https://unity.com/releases/editor/whats-new/6000.0.61f1)
