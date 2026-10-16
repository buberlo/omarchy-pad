#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc,char **argv) {
 setenv("FEX_DISABLETELEMETRY","1",1);
 setenv("FEX_ROOTFS","/opt/hollow-runtime",1);
 setenv("LD_LIBRARY_PATH","/opt/hollow-runtime/usr/lib/x86_64-linux-gnu",1);
 setenv("LD_PRELOAD","/opt/x86-game-crash-report.so",1);
 char *cmd[]={"/opt/fex/FEX-apple-m1","/opt/game-crash-report-probe",argc>1?"fatal":0,0};
 alarm(30);execv(cmd[0],cmd);perror("execv");return 1;
}
