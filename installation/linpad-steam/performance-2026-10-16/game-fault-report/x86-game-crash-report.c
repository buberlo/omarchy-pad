#define _GNU_SOURCE
#include <fcntl.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <sys/syscall.h>
#include <signal.h>
#include <stdint.h>
#include <ucontext.h>
#include <unistd.h>

// Diagnostic-only observer. Do not install globally or use for performance runs.
static unsigned char alternate_stack[65536];
static int (*real_sigaction)(int,const struct sigaction *,struct sigaction *);
static struct sigaction requested[NSIG];
static volatile sig_atomic_t observed[NSIG], reports;
static __thread unsigned recursion;
static int watched(int n) { return n==SIGSEGV || n==SIGBUS || n==SIGILL || n==SIGABRT; }
static int resolve(void) {
    if (!real_sigaction) real_sigaction=dlsym(RTLD_NEXT,"sigaction");
    if (!real_sigaction) { errno=ENOSYS; return -1; }
    return 0;
}
static size_t append(char *out, size_t pos, const char *text) {
    while (*text) out[pos++] = *text++;
    return pos;
}
static size_t hex(char *out, size_t pos, uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    pos = append(out, pos, "0x");
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        unsigned d = (value >> i) & 15;
        if (d || started || !i) { out[pos++] = digits[d]; started = 1; }
    }
    return pos;
}
static uint64_t parse_hex(const char **text) {
    uint64_t value = 0;
    for (;;) {
        unsigned char c = **text;
        unsigned d = c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : 16;
        if (d == 16) return value;
        value = (value << 4) | d;
        (*text)++;
    }
}
static void map_for(uint64_t rip, uint64_t fault) {
    int fd = open("/proc/self/maps", O_RDONLY|O_CLOEXEC);
    if (fd < 0) return;
    char input[4096], line[1024]; size_t used = 0, total = 0;
    ssize_t n;
    while (total < 262144 && (n = read(fd, input, sizeof(input))) > 0) {
        total += n;
        for (ssize_t i = 0; i < n; i++) {
            if (input[i] != '\n') { if (used + 1 < sizeof(line)) line[used++] = input[i]; continue; }
            line[used] = 0;
            const char *p = line;
            uint64_t low = parse_hex(&p), high = *p == '-' ? (++p, parse_hex(&p)) : 0;
            if ((rip >= low && rip < high) || (fault >= low && fault < high)) {
                write(2, "X86_MAP ", 8); write(2, line, used); write(2, "\n", 1);
                if (rip >= low && rip < high) {
                    while (*p == ' ') p++;
                    while (*p && *p != ' ') p++; // permissions
                    while (*p == ' ') p++;
                    uint64_t file_offset = parse_hex(&p);
                    char note[100]; size_t n = append(note,0,"X86_RIP_FILE_OFFSET ");
                    n=hex(note,n,rip-low+file_offset); note[n++]='\n'; write(2,note,n);
                }
            }
            used = 0;
        }
    }
    close(fd);
}
static void report(int signal_number, siginfo_t *info, void *context) {
    const ucontext_t *uc = context;
    char out[1024]; size_t pos = append(out, 0, "X86_FAULT signal=");
    pos = hex(out, pos, signal_number);
    pos = append(out, pos, " code="); pos = hex(out, pos, (int64_t)info->si_code);
    pos = append(out, pos, " address="); pos = hex(out, pos, (uintptr_t)info->si_addr);
    const struct { const char *name; int index; } registers[] = {
        {" rip=",REG_RIP},{" rsp=",REG_RSP},{" rbp=",REG_RBP},
        {" rax=",REG_RAX},{" rbx=",REG_RBX},{" rcx=",REG_RCX},
        {" rdx=",REG_RDX},{" rsi=",REG_RSI},{" rdi=",REG_RDI},
    };
    for (size_t i = 0; i < sizeof(registers)/sizeof(registers[0]); i++) {
        pos = append(out, pos, registers[i].name);
        pos = hex(out, pos, uc->uc_mcontext.gregs[registers[i].index]);
    }
    out[pos++]='\n'; write(2,out,pos);
    map_for(uc->uc_mcontext.gregs[REG_RIP],(uintptr_t)info->si_addr);

}

static void observer(int n, siginfo_t *info, void *context) {
    if (++recursion > 4) _exit(128+n);
    if (reports++ < 32) report(n,info,context);
    struct sigaction action=requested[n];
    if (action.sa_flags & SA_RESETHAND) requested[n].sa_handler=SIG_DFL;
    if (action.sa_handler==SIG_IGN) { --recursion; return; }
    if (action.sa_handler==SIG_DFL || !observed[n]) {
        struct sigaction reset={.sa_handler=SIG_DFL};
        sigemptyset(&reset.sa_mask);
        real_sigaction(n,&reset,0);
        // Queue the original fatal signal; delivery occurs on handler return.
        if (syscall(SYS_tgkill,getpid(),syscall(SYS_gettid),n)<0) _exit(128+n);
        --recursion; return;
    }
    // Clear depth before chaining: a legitimate siglongjmp does not return.
    --recursion;
    if (action.sa_flags & SA_SIGINFO) action.sa_sigaction(n,info,context);
    else action.sa_handler(n);
}
int sigaction(int n,const struct sigaction *action,struct sigaction *old) {
    if (resolve()) return -1;
    if (!watched(n)) return real_sigaction(n,action,old);
    struct sigaction previous;
    if (real_sigaction(n,0,&previous)) return -1;
    if (observed[n] && previous.sa_sigaction==observer) previous=requested[n];
    if (!action) { if(old)*old=previous; return 0; }
    struct sigaction logical=*action, wrapped=*action, before=requested[n];
    sig_atomic_t was_observed=observed[n];
    if (logical.sa_handler!=SIG_IGN) {
        wrapped.sa_sigaction=observer; wrapped.sa_flags|=SA_SIGINFO;
    }
    requested[n]=logical; observed[n]=1;
    if (real_sigaction(n,&wrapped,0)) { requested[n]=before;observed[n]=was_observed;return -1; }
    if(old)*old=previous;
    return 0;
}
typedef void (*handler_t)(int);
handler_t signal(int n,handler_t handler) {
    struct sigaction action={.sa_handler=handler,.sa_flags=SA_RESTART},old;
    sigemptyset(&action.sa_mask);
    if(sigaction(n,&action,&old))return SIG_ERR;
    return old.sa_handler;
}
__attribute__((constructor)) static void install_reporter(void) {
    if(resolve())return;
    stack_t old_stack;
    if(sigaltstack(0,&old_stack)==0 && (old_stack.ss_flags&SS_DISABLE)) {
        stack_t ss={.ss_sp=alternate_stack,.ss_size=sizeof(alternate_stack)};
        sigaltstack(&ss,0);
    }
    const int signals[]={SIGSEGV,SIGBUS,SIGILL,SIGABRT};
    for(unsigned i=0;i<sizeof(signals)/sizeof(signals[0]);i++) {
        struct sigaction current;
        if(real_sigaction(signals[i],0,&current)==0) {
            current.sa_flags|=SA_ONSTACK;
            sigaction(signals[i],&current,0);
        }
    }
    static const char ready[]="X86_FAULT_REPORTER_READY (diagnostic; handlers chained)\n";
    write(2,ready,sizeof(ready)-1);
}
