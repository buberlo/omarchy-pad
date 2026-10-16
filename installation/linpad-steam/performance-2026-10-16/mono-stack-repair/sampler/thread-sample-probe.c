#define _GNU_SOURCE
#include "sample-state.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
static volatile int done,futex_word;
static volatile pid_t tids[2];
static volatile sig_atomic_t app_hits;
static int failures;
static void check(int ok,const char *name){printf("SAMPLE_TEST %s %s\n",name,ok?"PASS":"FAIL");failures+=!ok;}
static void *busy(void *unused){(void)unused;tids[0]=syscall(SYS_gettid);while(!done)__asm__ volatile("pause");return NULL;}
static void *blocked(void *unused){(void)unused;tids[1]=syscall(SYS_gettid);while(!done)syscall(SYS_futex,&futex_word,128,0,NULL,NULL,0);return NULL;}
static void app_handler(int n){if(n==SAMPLE_SIGNAL)app_hits++;}
static int helper(pid_t tid){
    pid_t child=fork();if(child==0){char pid[20],t[20];snprintf(pid,sizeof(pid),"%d",getppid());snprintf(t,sizeof(t),"%d",tid);execl("/opt/thread-sample-send","thread-sample-send",pid,t,NULL);_exit(127);}
    int status;while(waitpid(child,&status,0)<0&&errno==EINTR){}return WIFEXITED(status)?WEXITSTATUS(status):128;
}
int main(int argc,char **argv){
    int external=argc>1&&!strcmp(argv[1],"external");
    setvbuf(stdout,NULL,_IOLBF,0);alarm(25);
    char path[96];sample_path(path,sizeof(path),getpid());struct sample_state st;
    int fd=open(path,O_RDONLY);check(fd>=0&&read(fd,&st,sizeof(st))==sizeof(st)&&st.active==1,"active token");if(fd>=0)close(fd);
    struct sigaction q;check(sigaction(SAMPLE_SIGNAL,NULL,&q)==0&&q.sa_handler==SIG_DFL,"logical default query");
    pthread_t a,b;check(pthread_create(&a,NULL,busy,NULL)==0,"busy thread created");check(pthread_create(&b,NULL,blocked,NULL)==0,"futex thread created");
    while(!tids[0]||!tids[1])usleep(1000);
    if(external){
        puts("SAMPLE_TARGET_THREADS_READY");
        struct timespec begin,now;clock_gettime(CLOCK_MONOTONIC,&begin);
        do {usleep(50000);clock_gettime(CLOCK_MONOTONIC,&now);}while(now.tv_sec-begin.tv_sec<4);
    }else{
        check(helper(getpid())==0,"native helper samples main");
        check(helper(tids[0])==0,"native helper samples running thread");
        check(helper(tids[1])==0,"native helper samples futex thread");
    }
    usleep(200000);done=1;__atomic_store_n(&futex_word,1,__ATOMIC_SEQ_CST);syscall(SYS_futex,&futex_word,129,1,NULL,NULL,0);
    check(pthread_join(a,NULL)==0&&pthread_join(b,NULL)==0,"both threads resume and join");
    struct sigaction app={.sa_handler=app_handler},old;sigemptyset(&app.sa_mask);
    check(sigaction(SAMPLE_SIGNAL,&app,&old)==0&&old.sa_handler==SIG_DFL,"app ownership preserved");
    syscall(SYS_tgkill,getpid(),syscall(SYS_gettid),SAMPLE_SIGNAL);check(app_hits==1,"app handler receives signal");
    fd=open(path,O_RDONLY);check(fd>=0&&read(fd,&st,sizeof(st))==sizeof(st)&&st.active==0,"replacement disarms token");if(fd>=0)close(fd);
    if(!external)check(helper(getpid())==2,"native helper refuses disarmed target");
    else sleep(2);
    check(app_hits==1,"refused helper never signals app handler");
    printf("SAMPLE_TEST_COMPLETE failures=%d\n",failures);return failures?1:0;
}
