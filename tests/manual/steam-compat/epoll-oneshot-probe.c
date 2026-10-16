#define _GNU_SOURCE
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
static int failures;
static void check(const char *name,int ok) { printf("%s %s errno=%d\n",name,ok?"PASS":"FAIL",errno);failures+=!ok; }
int main(void) {
    int ep=epoll_create1(0), e=eventfd(1,EFD_NONBLOCK),p[2];
    if(ep<0||e<0||pipe(p))return 90;
    struct epoll_event ev={.events=EPOLLIN|EPOLLONESHOT|EPOLLET,.data.u64=123},out;
    check("eventfd add",epoll_ctl(ep,EPOLL_CTL_ADD,e,&ev)==0);
    check("initial event",epoll_wait(ep,&out,1,0)==1&&out.data.u64==123);
    check("oneshot remains registered",epoll_ctl(ep,EPOLL_CTL_ADD,e,&ev)==-1&&errno==EEXIST);
    check("disabled no delivery",epoll_wait(ep,&out,1,0)==0);
    ev.data.u64=456;
    check("MOD rearm",epoll_ctl(ep,EPOLL_CTL_MOD,e,&ev)==0);
    check("rearm ready edge and data",epoll_wait(ep,&out,1,0)==1&&out.data.u64==456);
    check("DEL disabled",epoll_ctl(ep,EPOLL_CTL_DEL,e,0)==0);
    ev=(struct epoll_event){.events=EPOLLIN|EPOLLONESHOT,.data.u64=789};
    check("pipe add",epoll_ctl(ep,EPOLL_CTL_ADD,p[0],&ev)==0);close(p[1]);
    check("HUP initial",epoll_wait(ep,&out,1,0)==1&&(out.events&EPOLLHUP));
    check("HUP disabled",epoll_wait(ep,&out,1,0)==0);
    check("HUP rearm MOD",epoll_ctl(ep,EPOLL_CTL_MOD,p[0],&ev)==0);
    check("HUP after MOD",epoll_wait(ep,&out,1,0)==1&&(out.events&EPOLLHUP));
    close(p[0]);close(e);close(ep);return failures;
}
