#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc != 4) return 64;
    setenv("FEX_DISABLETELEMETRY", "1", 1);
    setenv("FEX_ROOTFS", "/opt/hollow-runtime", 1);
    setenv("LD_LIBRARY_PATH", "/opt/hollow-runtime/usr/lib/x86_64-linux-gnu:/opt/hollow-runtime/usr/lib/x86_64-linux-gnu/pulseaudio", 1);
    /* Local no-display comparison only; this is not the game's Wayland config. */
    setenv("SDL_VIDEODRIVER", "dummy", 1);
#ifdef MONO_PROBE_NOMB
    setenv("FEX_MULTIBLOCK", "0", 1);
#endif
#if defined(MONO_PROBE_MPROTECT)
    setenv("LD_PRELOAD", "/opt/mono-init/mprotect-observer.so", 1);
#elif defined(MONO_PROBE_REPORT)
    setenv("LD_PRELOAD", "/opt/x86-game-crash-report.so", 1);
#else
    unsetenv("LD_PRELOAD");
#endif
    char *args[] = {"/opt/fex/FEX-apple-m1-munmap", "/opt/mono-init/probe", argv[1], argv[2], argv[3], NULL};
    execv(args[0], args);
    perror("execv");
    return 1;
}
