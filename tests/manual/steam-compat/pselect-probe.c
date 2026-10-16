#define _GNU_SOURCE
#include <sys/select.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
int main(void) {
    int p[2], failures=0;
    if (pipe(p)) return 90;
    for(int packed=0;packed<2;packed++) {
        struct { uint64_t ptr,size; } mask={0,8};
        fd_set fds; FD_ZERO(&fds); FD_SET(p[0],&fds);
        struct timespec ts={0,0}; errno=0;
        long r=syscall(SYS_pselect6,p[0]+1,&fds,0,0,&ts,packed?&mask:0);
        printf("pselect6 empty mask=%s result=%ld errno=%d\n",packed?"packed-null":"null",r,errno);
        failures+=r!=0;
        write(p[1],"x",1); FD_ZERO(&fds); FD_SET(p[0],&fds); ts=(struct timespec){0,0};errno=0;
        r=syscall(SYS_pselect6,p[0]+1,&fds,0,0,&ts,packed?&mask:0);
        printf("pselect6 ready mask=%s result=%ld errno=%d\n",packed?"packed-null":"null",r,errno);
        failures+=r!=1 || !FD_ISSET(p[0],&fds);
        char c;read(p[0],&c,1);
    }
    close(p[0]);close(p[1]);return failures;
}
