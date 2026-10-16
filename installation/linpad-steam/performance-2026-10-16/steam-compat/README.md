# LinPad native Linux Steam tool — installed and selected

This directory contains the custom Linux-to-Linux compatibility tool installed
on the physical iPad. Steam recognizes **LinPad FEX (Linux Vulkan)** and Hollow
Knight's Compatibility dropdown visibly shows it selected. Native depot
verification, game launch and gameplay remain **unverified**. Build 4443's direct
x86 Wayland Vulkan cube completed bounded 120-frame and 1200-frame runs, and two
captures show visible rotation. Game qualification remains a separate gate.

The installed toolkit archive SHA-256 is `2aa19f7f9b3a5c6ea3e7ffd3e2b32977babed57120a01c64309b3ff6cfcd6ae1`.
The device task recorded [Steam selection](steam-native-tool-selected.png).
Hollow Knight's launch options were set through Steam's UI to
`-force-vulkan -screen-width 960 -screen-height 540 -screen-fullscreen 0`.
Steam Overlay was disabled for the game; Steam Cloud settings were unchanged.
This README includes later device status; the installed archive has not been
repacked. The adjacent source checksum list describes this directory's current
files, including these documentation updates.

## Known state

- `/opt/fex/FEX-apple-m1` passed small x86 runtime tests on the physical iPad.
- The base runtime `/opt/hollow-runtime` contains amd64 glibc and Vulkan tools.
  The default Mesa 26.2.4 Venus overlay is `/opt/hollow-mesa26`.
- `vkcube --help` works when full host library paths are provided.
- Mesa 25.0.7 did not expose `VK_KHR_swapchain` on this host. Mesa 26.2.4 exposes
  it; a physical headless probe completed device/queue creation, idle wait and
  teardown successfully. An initial direct Wayland cube stalled after a valid
  image. Build 4443 includes the GPU fence lock inversion repair and deterministic
  regression: the physical headless probe exited 0 in 8.85 seconds, and the cube
  completed 120 frames in 38.12 seconds with exit 0 and a working report upload.
  That duration includes startup; it is not a measured steady-state game FPS.
- FEX's rootfs redirection calls `openat2(...RESOLVE_IN_ROOT)`. LinPad returns
  ENOSYS for that syscall. FEX only retries openat for EXDEV, so the launch script
  supplies full host library/ICD paths. This is a workaround, not a syscall fix.

## Installation and Steam selection

Extract the archive inside LinPad and run `sh install.sh` from its directory.
It locates the current user's existing Steam directory by checking directories
only, or accepts `LINPAD_STEAM_ROOT=/absolute/Steam/path sh install.sh`. It copies
the tool's four files under `compatibilitytools.d/linpad-fex-native`, skips an
identical installation and preserves a replaced tool under `linpad-tool-backups`.
It does not restart Steam, read credentials, edit account/configuration files,
change compatibility selection or modify downloads.

1. Restart Steam at the coordinated point, then open **Library → Hollow Knight →
   Properties → Compatibility**.
2. In **Select compatibility tool**, choose **LinPad FEX (Linux Vulkan)**. The
   observed 2026 Steam client provides this dropdown directly; its previous value
   was **Default (Proton Stable)**. There is no force-compatibility checkbox in
   this interface.
3. Let Steam finish the resulting native Linux update. In **Installed Files →
   Browse**, confirm `hollow_knight.x86_64` is present.
4. Run `sh verify-native.sh /absolute/Steam/library` with the directory containing
   `steamapps`. It reads only `appmanifest_367520.acf` and the game's ELF header,
   requiring Linux depot **367523** in `InstalledDepots` and the x86-64 ELF.
5. Inspect the actual launch/log before accepting integration. Test rendering,
   input, audio and sustained frame progression before calling the game playable.

Registration uses `from_oslist=linux` and `to_oslist=linux`. It does not declare
undocumented architecture keys or require a Steam container runtime. The Linux
depot identifier should also be confirmed against current Steam app metadata;
the verifier only checks the installed record and executable.

The ARM64 Steam client contains an automatic FEX-layer selection path. Inspect
the actual launch command and compatibility log for unintended additional layers.
Restarting Steam and changing compatibility selection affect active downloads;
do this only at the coordinated point selected by the main task.

## Launcher behavior

`run` accepts Steam's `run` and `waitforexitandrun` verbs, removes its own `--`
separator, and forwards the remaining arguments exactly to `launch-game`.
Both scripts preserve Steam's working directory and app identifiers.

`launch-game` defaults to a fresh `ishwl-x11` display at **960×540**, connected
to `wayland-0-gpu` under `/tmp/xdg-runtime`.
This avoids inheriting Steam's software Xvnc display `:55`. Direct Xwayland
Vulkan presentation is a candidate path, not yet a verified one. The script
uses the existing `/opt/steam-x11/bin/Xwayland` wrapper, when present, via PATH.
Xwayland starts with the ARM64 Steam library and software-GL environment cleared;
the x86 library paths are added only after the new display is ready.

The Vulkan driver is explicitly the x86 Venus ICD. `MESA_VK_WSI_DEBUG=sw`
requests shared-memory presentation; it does not select a CPU Vulkan renderer.
`VK_LOADER_LAYERS_DISABLE=*` isolates the x86 driver from inherited Steam
implicit layers of mixed architectures. The successful probes used this
isolation; it does not establish that those layers caused the separate JIT bug.
No OpenGL version override, TSO weakening, GPU feature spoofing, credential
handling or change to Steam account data is included.

## Configuration

| Variable | Default / effect |
| --- | --- |
| `LINPAD_FEX_BINARY` | `/opt/fex/FEX-apple-m1` |
| `LINPAD_FEX_RUNTIME` | `/opt/hollow-runtime` |
| `LINPAD_FEX_ROOTFS` | Runtime prefix; an explicitly empty value is preserved |
| `LINPAD_FEX_ICD` | `/opt/hollow-mesa26/usr/share/vulkan/icd.d/virtio_icd.json` |
| `LINPAD_FEX_LIBRARY_OVERLAY` | `/opt/hollow-mesa26/usr/lib/x86_64-linux-gnu`, prepended; an explicitly empty value disables the overlay |
| `LINPAD_FEX_EXTRA_LIBRARY_PATH` | Optional additional **x86** library directories, appended |
| `LINPAD_FEX_DISPLAY_MODE` | `x11`; alternatives: `wayland`, `existing`, `none` |
| `LINPAD_FEX_X11_RUNNER` | `ishwl-x11`, must support its `-g WxH command…` interface |
| `LINPAD_FEX_GEOMETRY` | `960x540` |
| `LINPAD_FEX_XDG_RUNTIME_DIR` | `/tmp/xdg-runtime`; overrides Steam's inherited value |
| `LINPAD_FEX_WAYLAND_DISPLAY` | `wayland-0-gpu` |
| `LINPAD_FEX_APP_NAME` | `Hollow Knight` |
| `LINPAD_FEX_FORCE_VULKAN` | `0`; `1` appends Unity's `-force-vulkan` option |

`wayland` mode is experimental for this Unity game; the caller must supply any
engine-specific flags. `existing` preserves DISPLAY for explicit diagnostics,
including potentially slow Steam Xvnc. `none` clears presentation variables for
command-line tests. FEX is run in the foreground with an 8 MiB shell stack limit.

Direct probe example after copying the staged launcher to a chosen device path:

```sh
LINPAD_FEX_DISPLAY_MODE=none /chosen/path/launch-game \
  /opt/hollow-runtime/usr/bin/vkcube --help
```

Direct game example from the installed Linux game directory:

```sh
LINPAD_FEX_FORCE_VULKAN=1 /chosen/path/launch-game ./hollow_knight.x86_64
```

## Provenance

- Registration schema: [Valve Proton's current template](https://github.com/ValveSoftware/Proton/blob/proton_11.0/compatibilitytool.vdf.template).
- Command protocol and lifecycle: [Valve/Collabora interface specification](https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/-/blob/main/docs/steam-compat-tool-interface.md).
  The documentation's first example contains `compat tools`; the actual Proton
  template and Steam binary use `compat_tools`, which these files follow.
- Linux-to-Linux selection precedent: [original Steam-Play-None implementation](https://github.com/Scrumplex/Steam-Play-None).
- Hollow Knight Linux renderer background: [Team Cherry patch notes](https://store.steampowered.com/news/app/367520/view/3003320140223424876). The launcher does not assume the installed game version or append engine flags by default.
- Existing display contract: local LinPad `wl-bridge/ishwl-x11`, GPU defaults in
  `gpu/rootfs-add-gpu.sh`, and the recorded `work/steam/serve/xwayland-wrapper`.
- The custom shell scripts here are new local integration code, not modifications
  to upstream FEX. No FEX source files were changed.

`VALIDATION.txt` records local shell syntax and mocked argv/environment checks.
These validate wrapper behavior only, not Steam integration or game compatibility.
Run `python3 validate-launcher.py` on the local host to repeat those checks.
`INSTALL-VALIDATION.txt` records synthetic installation and native-depot checks;
repeat them locally with `python3 validate-install.py`.
