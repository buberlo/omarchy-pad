# Minimal x86 libGL repair for LinPad

Unity build 6000.0.61f1 reported that MainPlayerWindow creation failed because libGL.so.1 was missing. This package adds only official Debian trixie amd64 GLVND libraries libGL.so.1, libGLX.so.0, and libGLdispatch.so.0 (libgl1/libglx0/libglvnd0 version 1.7.0-1+b2), plus their copyright files.

Run install.sh from the unpacked directory. It only adds files under /opt/hollow-runtime after verifying that runtime's libc, loader and X11 hashes. It verifies payload hashes and symlinks, retains matching existing files, and refuses different existing files before writes. Save its ADDED lines as the exact rollback list. It does not change Steam, FEX, Mesa, account data, or game configuration.

Local ARM64 Linux FEX test passed: libGL missing before repair, dlopen(RTLD_NOW) and three GL/GLX symbols resolve afterward. Installer idempotence and conflicting-file refusal also passed. This validates the library loader only, not creation of a GL context or Hollow Knight gameplay.

Debian's libglx0 package also declares libglx-mesa0 as a vendor implementation dependency. That package is deliberately absent from this narrow loader repair: the three GLVND libraries load successfully without it and this game's requested renderer is Vulkan. A real GLX context may still require a vendor implementation; evaluate only if the next device error requires it.

Physical iPad validation on LinPad build 4445 also passed: the archived `glvnd-device-install.txt` shows archive/runtime/payload hashes accepted, the exact nine added entries, successful library load and three symbol resolutions, and `GLVND_DEVICE_LIBGL_PASS`. The served wrapper unsets `LD_PRELOAD`. The report contains no timestamp; the device operator identified this as the 2026-10-16 build 4445 run. The installed tar archive is kept byte-for-byte unchanged; this later README/provenance update and device report are stored alongside it. Window creation and gameplay remain separate checks.
