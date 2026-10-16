#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
    setenv("FEX_DISABLETELEMETRY", "1", 1);
    setenv("FEX_ROOTFS", "/opt/hollow-runtime", 1);
    setenv("LD_LIBRARY_PATH", "/opt/hollow-runtime/usr/lib/x86_64-linux-gnu", 1);
    unsetenv("LD_PRELOAD");
    unsetenv("FEX_MULTIBLOCK");
    if (argc > 1 && strcmp(argv[1], "default")) setenv("FEX_HOSTFEATURES", argv[1], 1);
    else unsetenv("FEX_HOSTFEATURES");
    const char *target = argc > 2 ? argv[2] : "/opt/fex/hello-x86_64";
    printf("RCPC_CONTROL features=%s target=%s\n", getenv("FEX_HOSTFEATURES") ?: "default", target);
    fflush(stdout);
    char *cmd[] = {"/opt/fex/FEX-apple-m1-munmap", (char *)target, NULL};
    execv(cmd[0], cmd);
    perror("execv");
    return 1;
}
