#define _GNU_SOURCE
#include "sample-state.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <sys/syscall.h>
#include <ucontext.h>

/* Opt-in diagnostic only. SIGURG defaults to ignore if ownership is lost.
 * Never reserve a signal already handled or deliberately ignored by the app.
 * No unwinder, allocation, dladdr or /proc traversal in the signal handler. */
static int (*next_sigaction)(int,const struct sigaction*,struct sigaction*);
static volatile sig_atomic_t owned;
static int state_fd=-1;
static struct sigaction original;
static int resolve(void) {
    if(!next_sigaction)next_sigaction=dlsym(RTLD_NEXT,"sigaction");
    if(!next_sigaction){errno=ENOSYS;return -1;}return 0;
}
static size_t text(char *out,size_t pos,const char *s){while(*s)out[pos++]=*s++;return pos;}
static size_t hx(char *out,size_t pos,uint64_t v){
    static const char ds[]="0123456789abcdef";pos=text(out,pos,"0x");int seen=0;
    for(int i=60;i>=0;i-=4){unsigned d=(v>>i)&15;if(d||seen||!i){out[pos++]=ds[d];seen=1;}}
    return pos;
}
static void sample(int n,siginfo_t *si,void *ctx){
    int saved=errno;const ucontext_t *uc=ctx;char line[640];size_t p=text(line,0,"X86_SAMPLE tid=");
    p=hx(line,p,syscall(SYS_gettid));p=text(line,p," signal=");p=hx(line,p,n);
    p=text(line,p," code=");p=hx(line,p,(int64_t)si->si_code);
    const struct {const char *name;int reg;} regs[]={
        {" rip=",REG_RIP},{" rsp=",REG_RSP},{" rbp=",REG_RBP},
        {" rax=",REG_RAX},{" rbx=",REG_RBX},{" rcx=",REG_RCX},
        {" rdx=",REG_RDX},{" rsi=",REG_RSI},{" rdi=",REG_RDI}};
    for(size_t i=0;i<sizeof(regs)/sizeof(regs[0]);i++){p=text(line,p,regs[i].name);p=hx(line,p,uc->uc_mcontext.gregs[regs[i].reg]);}
    line[p++]='\n';write(2,line,p);errno=saved;
}
static void disarm(void){
    owned=0;uint32_t inactive=0;
    if(state_fd>=0)pwrite(state_fd,&inactive,sizeof(inactive),offsetof(struct sample_state,active));
    static const char msg[]="X86_SAMPLE_DISARMED application claimed SIGURG\n";write(2,msg,sizeof(msg)-1);
}
int sigaction(int n,const struct sigaction *a,struct sigaction *old){
    if(resolve())return -1;
    if(n!=SAMPLE_SIGNAL||!owned)return next_sigaction(n,a,old);
    struct sigaction actual;if(next_sigaction(n,NULL,&actual))return -1;
    if(actual.sa_sigaction!=sample){disarm();return next_sigaction(n,a,old);}
    if(!a){if(old)*old=original;return 0;}
    /* Invalidate the helper token before handing the signal to the app. */
    disarm();int rc=next_sigaction(n,a,NULL);if(!rc&&old)*old=original;return rc;
}
typedef void (*handler_t)(int);
handler_t signal(int n,handler_t fn){
    struct sigaction a={.sa_handler=fn,.sa_flags=SA_RESTART},old;
    sigemptyset(&a.sa_mask);return sigaction(n,&a,&old)?SIG_ERR:old.sa_handler;
}
__attribute__((constructor)) static void install(void){
    const char *opt=getenv("LINPAD_X86_SAMPLE");if(!opt||strcmp(opt,"1")||resolve())return;
    if(next_sigaction(SAMPLE_SIGNAL,NULL,&original))return;
    if(original.sa_handler!=SIG_DFL){
        static const char msg[]="X86_SAMPLE_REFUSED SIGURG already owned\n";write(2,msg,sizeof(msg)-1);return;
    }
    char path[96];sample_path(path,sizeof(path),getpid());
    struct sample_state st={.magic=SAMPLE_MAGIC,.start_ticks=sample_start(getpid()),.pid=(uint32_t)getpid(),.signal=SAMPLE_SIGNAL,.active=0};
    if(!st.start_ticks)return;
    state_fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(state_fd<0)return;
    if(write(state_fd,&st,sizeof(st))!=sizeof(st)){close(state_fd);state_fd=-1;return;}
    struct sigaction a={.sa_sigaction=sample,.sa_flags=SA_SIGINFO|SA_RESTART};sigemptyset(&a.sa_mask);
    if(next_sigaction(SAMPLE_SIGNAL,&a,NULL)){close(state_fd);state_fd=-1;return;}
    owned=1;uint32_t active=1;
    if(pwrite(state_fd,&active,sizeof(active),offsetof(struct sample_state,active))!=sizeof(active)){owned=0;next_sigaction(SAMPLE_SIGNAL,&original,NULL);return;}
    static const char ready[]="X86_SAMPLE_READY signal=23 (SIGURG; opt-in; returns without termination)\n";write(2,ready,sizeof(ready)-1);
}
