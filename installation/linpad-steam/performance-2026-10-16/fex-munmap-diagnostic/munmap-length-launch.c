#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc,char **argv) {
 setenv("FEX_DISABLETELEMETRY","1",1);
 setenv("FEX_ROOTFS","/opt/hollow-runtime",1);
 setenv("LD_LIBRARY_PATH","/opt/hollow-runtime/usr/lib/x86_64-linux-gnu",1);
 if(argc>1) setenv("LD_PRELOAD","/opt/fex-munmap-round.so",1);
 char *cmd[]={"/opt/fex/FEX-apple-m1","/opt/munmap-length-x86",NULL};
 execv(cmd[0],cmd);perror("execv");return 2;
}
