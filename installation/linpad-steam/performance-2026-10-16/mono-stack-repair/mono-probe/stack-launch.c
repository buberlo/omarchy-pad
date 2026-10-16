#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    setenv("FEX_DISABLETELEMETRY", "1", 1);
    setenv("FEX_ROOTFS", "/opt/hollow-runtime", 1);
    setenv("LD_LIBRARY_PATH", "/opt/hollow-runtime/usr/lib/x86_64-linux-gnu", 1);
    unsetenv("LD_PRELOAD");
    char *args[] = {"/opt/fex/FEX-apple-m1-munmap", "/opt/mono-init/stack-limits-probe", NULL};
    execv(args[0], args);
    perror("execv");
    return 1;
}
