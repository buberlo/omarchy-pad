#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
int main(void) {
    unsetenv("LD_PRELOAD");unsetenv("FEX_MULTIBLOCK");
    setenv("FEX_DISABLETELEMETRY","1",1);
    setenv("FEX_ROOTFS","/opt/hollow-runtime",1);
    setenv("LD_LIBRARY_PATH","/opt/hollow-runtime/usr/lib/x86_64-linux-gnu",1);
    char *args[]={"/opt/fex/FEX-apple-m1-munmap","/opt/hollow-runtime/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2","--list","/opt/steam-sdk-x86/1792110651/linux64/steamclient.so",NULL};
    execv(args[0],args);perror("execv");return 1;
}
