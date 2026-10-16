#define _GNU_SOURCE
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pid_t process, parent;
static int proc_parent(const char *file) {
    FILE *f=fopen(file,"r");if(!f)return -1;
    char line[1024];int result=-1;
    if(strstr(file,"status")) {
        while(fgets(line,sizeof(line),f)) if(sscanf(line,"PPid: %d",&result)==1)break;
    } else if(fgets(line,sizeof(line),f)) {
        char *end=strrchr(line,')');char state;
        if(!end||sscanf(end+1," %c %d",&state,&result)!=2)result=-1;
    }
    fclose(f);return result;
}
static void *worker(void *unused) {
    (void)unused;int failed=0;
    int actual=getppid();
    printf("worker parent=%d expected=%d\n",actual,parent);
    if(actual!=parent)failed=1;
    pid_t child=fork();if(child<0)return (void *)90;
    if(!child) {
        int api=getppid(),status=proc_parent("/proc/self/status"),stat=proc_parent("/proc/self/stat");
        printf("child parent api=%d status=%d stat=%d expected=%d\n",api,status,stat,process);
        _exit(api!=process||status!=process||stat!=process);
    }
    int status;
    if(waitpid(child,&status,0)!=child||!WIFEXITED(status)||WEXITSTATUS(status))failed=1;
    return (void *)(size_t)failed;
}
int main(void) {
    setbuf(stdout,NULL);process=getpid();parent=getppid();
    pthread_t thread;if(pthread_create(&thread,NULL,worker,NULL))return 91;
    void *result;pthread_join(thread,&result);
    puts(result?"PARENT_PID_FAIL":"PARENT_PID_OK");return (int)(size_t)result;
}
