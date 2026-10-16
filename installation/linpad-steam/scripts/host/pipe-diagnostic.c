#define _GNU_SOURCE
#include <fcntl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
int pipe2(int p[2], int flags) {
    static int (*original)(int *, int);
    if (!original) original = dlsym(RTLD_NEXT, "pipe2");
    int effective = flags;
    /* Diagnostic only: not a packet-mode implementation. */
    if (getenv("STEAM_PIPE_STREAM_FALLBACK")) effective &= ~O_DIRECT;
    int result;
    if ((flags & O_DIRECT) && getenv("STEAM_PIPE_PACKET_FALLBACK")) {
        /* Steam-only experiment: preserve message boundaries for its small
         * controller notifications using LinPad's existing SEQPACKET pair.
         * These are sockets, not a general implementation of packet pipes. */
        result=socketpair(AF_UNIX,SOCK_SEQPACKET | (flags & (O_CLOEXEC|O_NONBLOCK)),0,p);
        if(result==0){shutdown(p[0],SHUT_WR);shutdown(p[1],SHUT_RD);}
    } else result = original(p, effective);
    int saved = errno;
    fprintf(stderr, "LINPAD_PIPE2 flags=%x result=%d errno=%d\n", flags, result, saved);
    errno = saved;
    return result;
}
int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*entry)(void *), void *arg) {
    static int (*original)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
    if (!original) original=dlsym(RTLD_NEXT,"pthread_create");
    size_t size=0,guard=0;void *stack=0;
    if(attr){pthread_attr_getstack(attr,&stack,&size);pthread_attr_getstacksize(attr,&size);pthread_attr_getguardsize(attr,&guard);}
    else {pthread_attr_t def;pthread_getattr_default_np(&def);pthread_attr_getstacksize(&def,&size);pthread_attr_destroy(&def);}
    fprintf(stderr,"LINPAD_THREAD attr=%p stack=%p size=%zx guard=%zx entry=%p\n",attr,stack,size,guard,entry);
    int r=original(thread,attr,entry,arg);
    fprintf(stderr,"LINPAD_THREAD result=%d\n",r);
    return r;
}
