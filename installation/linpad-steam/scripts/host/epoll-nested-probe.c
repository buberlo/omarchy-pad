#define _GNU_SOURCE
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
static int inner, counter, mod_result;
static void *rearm(void *unused) {
    (void)unused;usleep(30000);
    struct epoll_event ev={.events=EPOLLIN|EPOLLONESHOT,.data.u64=42};
    mod_result=epoll_ctl(inner,EPOLL_CTL_MOD,counter,&ev);return 0;
}
int main(void) {
    setbuf(stdout,NULL);
    inner=epoll_create1(0);int outer=epoll_create1(0);
    counter=eventfd(2,EFD_SEMAPHORE|EFD_NONBLOCK);
    struct epoll_event ev={.events=EPOLLIN|EPOLLONESHOT,.data.u64=42},out;
    if(inner<0||outer<0||counter<0||epoll_ctl(inner,EPOLL_CTL_ADD,counter,&ev))return 90;
    ev=(struct epoll_event){.events=EPOLLIN|EPOLLET,.data.u64=99};
    if(epoll_ctl(outer,EPOLL_CTL_ADD,inner,&ev)||epoll_wait(outer,&out,1,0)!=1||epoll_wait(inner,&out,1,0)!=1)return 91;
    uint64_t n;if(read(counter,&n,8)!=8||n!=1)return 92;
    if(epoll_wait(outer,&out,1,0)!=0)return 93;
    pthread_t worker;if(pthread_create(&worker,0,rearm,0))return 94;
    int result=epoll_wait(outer,&out,1,500);
    pthread_join(worker,0);
    printf("nested outer wake after inner MOD: result=%d mod=%d %s\n",result,mod_result,result==1&&mod_result==0?"PASS":"FAIL");
    int failed=result!=1||mod_result!=0;
    close(counter);close(outer);close(inner);return failed;
}
