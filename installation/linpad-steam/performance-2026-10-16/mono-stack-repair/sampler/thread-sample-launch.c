#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "sample-state.h"
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>
static int helper(pid_t pid){
 pid_t child=fork();if(child==0){char p[24];snprintf(p,sizeof(p),"%d",pid);execl("/opt/thread-sample-send","thread-sample-send",p,"all",NULL);_exit(127);}
 int status;waitpid(child,&status,0);return WIFEXITED(status)?WEXITSTATUS(status):128;
}
int main(void){
 setvbuf(stdout,NULL,_IOLBF,0);alarm(30);
 pid_t child=fork();if(child!=0){
   char path[96];sample_path(path,sizeof(path),child);struct sample_state st={0};
   for(int i=0;i<100;i++){int f=open(path,O_RDONLY);if(f>=0){read(f,&st,sizeof(st));close(f);}if(st.active==1)break;usleep(50000);}
   usleep(700000);int first=helper(child);printf("NATIVE_HELPER_ACTIVE_EXIT=%d\n",first);
   for(int i=0;i<120;i++){int f=open(path,O_RDONLY);if(f>=0){read(f,&st,sizeof(st));close(f);}if(st.active==0)break;usleep(50000);}
   int second=helper(child);printf("NATIVE_HELPER_DISARMED_EXIT=%d\n",second);
   int status;waitpid(child,&status,0);int rc=WIFEXITED(status)?WEXITSTATUS(status):128;
   printf("SAMPLE_ORCHESTRATOR child_exit=%d active_send=%d disarmed_refusal=%d\n",rc,first,second);return rc||first||second!=2;
 }
 setenv("FEX_DISABLETELEMETRY","1",1);
 setenv("FEX_ROOTFS","/opt/hollow-runtime",1);
 setenv("LD_LIBRARY_PATH","/opt/hollow-runtime/usr/lib/x86_64-linux-gnu",1);
 setenv("LINPAD_X86_SAMPLE","1",1);
 setenv("LD_PRELOAD","/opt/x86-thread-sample.so:/opt/x86-game-crash-report.so",1);
 char *cmd[]={"/opt/fex/FEX-apple-m1-munmap","/opt/thread-sample-probe","external",NULL};
 alarm(30);execv(cmd[0],cmd);perror("execv");return 1;
}
