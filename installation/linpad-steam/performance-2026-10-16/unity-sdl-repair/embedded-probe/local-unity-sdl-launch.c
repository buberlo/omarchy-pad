#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc != 2) return 64;
    setenv("FEX_DISABLETELEMETRY", "1", 1);
    setenv("FEX_ROOTFS", "/opt/hollow-runtime", 1);
    setenv("LD_LIBRARY_PATH", "/opt/hollow-runtime/usr/lib/x86_64-linux-gnu:/opt/hollow-runtime/usr/lib/x86_64-linux-gnu/pulseaudio", 1);
    alarm(55);
    char *u[] = {"/opt/fex/FEX-apple-m1", "/opt/unity-sdl-init-probe", "udev", NULL};
    char *s[] = {"/opt/fex/FEX-apple-m1", "/opt/unity-sdl-init-probe", "/opt/UnityPlayer.so", argv[1], NULL};
    execv(u[0], argv[1][0] == 'u' ? u : s);
    perror("execv");
    return 1;
}
