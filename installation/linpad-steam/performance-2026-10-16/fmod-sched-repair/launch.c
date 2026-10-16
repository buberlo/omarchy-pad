#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc,char **argv) {
    if(argc!=2) return 64;
    setenv("FEX_DISABLETELEMETRY","1",1);
    setenv("FEX_ROOTFS","/opt/hollow-runtime",1);
    setenv("LD_LIBRARY_PATH","/opt/hollow-runtime/usr/lib/x86_64-linux-gnu:/opt/hollow-runtime/usr/lib/x86_64-linux-gnu/pulseaudio",1);
    unsetenv("LD_PRELOAD");unsetenv("FEX_MULTIBLOCK");
    char *a[]={"/opt/fex/FEX-apple-m1-munmap",argv[1],NULL};
    execv(a[0],a);perror("execv");return 1;
}
