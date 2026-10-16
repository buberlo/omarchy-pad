#define _GNU_SOURCE
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

// Run actual LRCPC2 guest instructions, including spilled registers, SP,
// misalignment and split pages. These tests target LinPad's existing gadget
// semantics as well as its native JIT; they are not host-hardware atomics tests.
static unsigned checks, failures;
#define CHECK(v) do { checks++; if (!(v)) { if (failures++ < 12) fprintf(stderr,"FAIL line=%d: %s\n",__LINE__,#v); } } while (0)
typedef uint64_t (*op_fn)(void *, uint64_t);
static uint32_t *code;
static size_t used;
static uintptr_t last_op_pc;
static uint32_t mov(int rd, int rn) { return 0xaa0003e0u | (rn << 16) | rd; }
static void emit(uint32_t word) { code[used++] = word; }
static op_fn make_op(int size, int opc, int base, int data, int offset) {
    size_t start = used;
    for (int r=19;r<31;r+=2) emit(0xa9bf0000u | ((r+1)<<10) | (31<<5) | r);
    int save_sp = data == 9 ? 10 : 9;
    if (base == 31) {
        emit(0x910003e0u | save_sp); // mov save_sp, sp
        emit(0x9100001fu);          // mov sp, x0
    } else {
        if (base == 1 && data != 1 && data != 31) emit(mov(2,1));
        if (base != 0) emit(mov(base,0));
    }
    if (data != 31 && data != base)
        emit(mov(data, base == 1 ? 2 : 1));
    last_op_pc=(uintptr_t)&code[used];
    emit(((uint32_t)size<<30) | 0x19000000u | (opc<<22) |
         (((uint32_t)offset&511)<<12) | (base<<5) | data);
    emit(mov(0,data));
    if (base == 31) emit(0x9100001fu | (save_sp<<5)); // restore sp
    for (int r=29;r>=19;r-=2) emit(0xa8c10000u | ((r+1)<<10) | (31<<5) | r);
    emit(0xd65f03c0u);
    __builtin___clear_cache((char *)&code[start],(char *)&code[used]);
    return (op_fn)&code[start];
}
static uint64_t mask_size(int size) { return size == 3 ? UINT64_MAX : (UINT64_C(1) << (8<<size))-1; }
static uint64_t loaded(uint64_t raw,int size,int opc) {
    raw &= mask_size(size);
    if (opc >= 2) {
        int bits=8<<size;
        raw=(uint64_t)((int64_t)(raw << (64-bits)) >> (64-bits));
        if (opc == 3) raw=(uint32_t)raw;
    }
    return raw;
}
static void values(unsigned char *mem) {
    const int bases[]={0,1,11,13,18,29,31};
    const int regs[]={0,1,9,11,14,18,19,29,30,31};
    const int offsets[]={-256,0,255};
    const int locations[]={1024,1025,4095};
    const uint64_t pattern=UINT64_C(0xf1e2d3c4b5a69788);
    unsigned local=0;
    for(int size=0;size<4;size++) for(int opc=0;opc<4;opc++) {
        if ((size==3 && opc>=2)||(size==2 && opc==3)) continue;
        for(unsigned b=0;b<sizeof(bases)/sizeof(*bases);b++)
        for(unsigned r=0;r<sizeof(regs)/sizeof(*regs);r++)
        for(unsigned o=0;o<sizeof(offsets)/sizeof(*offsets);o++) {
            int base=bases[b],data=regs[r],off=offsets[o];
            op_fn fn=make_op(size,opc,base,data,off);
            for(unsigned a=0;a<sizeof(locations)/sizeof(*locations);a++) {
                unsigned char *at=mem+locations[a];
                void *address=at-off;
                memset(at-8,0x5a,32);
                memcpy(at,&pattern,8);
                uint64_t actual=fn(address,pattern),raw=0;
                memcpy(&raw,at,1u<<size);
                if (opc==0) {
                    uint64_t expected=data==31?0:data==base?(uintptr_t)address:pattern;
                    CHECK(raw == (expected & mask_size(size)));
                    CHECK(at[-1]==0x5a && at[8]==0x5a);
                    // A following ordinary load must already see a crossing release store.
                    CHECK(actual == (data==31?0:expected));
                } else {
                    CHECK(actual == (data==31?0:loaded(pattern,size,opc)));
                    CHECK(raw == (pattern & mask_size(size)));
                }
                local++;
            }
        }
    }
    printf("LRCPC2_VALUES cases=%u\n",local);fflush(stdout);
}
static sigjmp_buf jump;
static volatile sig_atomic_t signal_seen;
static uintptr_t fault_addr,fault_pc,fault_x0;
static void handler(int sig,siginfo_t *info,void *v) {
    ucontext_t *u=v;signal_seen=sig;fault_addr=(uintptr_t)info->si_addr;
    fault_pc=u->uc_mcontext.pc;fault_x0=u->uc_mcontext.regs[0];
    siglongjmp(jump,1);
}
static void faults(unsigned char *mem) {
    struct sigaction sa={.sa_sigaction=handler,.sa_flags=SA_SIGINFO};sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV,&sa,NULL);sigaction(SIGBUS,&sa,NULL);sigaction(SIGILL,&sa,NULL);
    CHECK(mprotect(mem+4096,4096,PROT_NONE)==0);
    for(int store=0;store<2;store++) {
        op_fn fn=make_op(3,store?0:1,1,0,0);
        uintptr_t op_pc=last_op_pc;
        uintptr_t addrs[]={0,1,(uintptr_t)(mem+4096),(uintptr_t)(mem+4095)};
        for(unsigned a=0;a<4;a++) {
            signal_seen=0;fault_pc=0;
            if(sigsetjmp(jump,1)==0) { fn((void *)addrs[a],0x1122334455667788ull);CHECK(0); }
            else {
                printf("LRCPC2_FAULT store=%d case=%u signal=%d address=%#" PRIxPTR " expected_address=%#" PRIxPTR " pc=%#" PRIxPTR " expected_pc=%#" PRIxPTR "\n",
                       store,a,signal_seen,fault_addr,a==3?(uintptr_t)(mem+4096):addrs[a],fault_pc,op_pc);
                CHECK(signal_seen==SIGSEGV);
                CHECK(fault_addr==(a==3?(uintptr_t)(mem+4096):addrs[a]));
                CHECK(fault_pc==op_pc);
                CHECK(fault_x0==0x1122334455667788ull);
            }
        }
    }
    CHECK(mprotect(mem+4096,4096,PROT_READ)==0);
    op_fn store=make_op(3,0,1,0,0);uintptr_t op_pc=last_op_pc;
    signal_seen=0;
    if(sigsetjmp(jump,1)==0) { store(mem+4096,123);CHECK(0); }
    else {CHECK(signal_seen==SIGSEGV);CHECK(fault_addr==(uintptr_t)(mem+4096));CHECK(fault_pc==op_pc);CHECK(fault_x0==123);}
    CHECK(mprotect(mem+4096,4096,PROT_READ|PROT_WRITE)==0);
    // Reserved size/opc combinations must fault, not become PRFUM no-ops.
    for(int i=0;i<3;i++) {
        int size=i<2?3:2,opc=i<2?i+2:3;
        op_fn fn=make_op(size,opc,1,0,0);uintptr_t pc=last_op_pc;signal_seen=0;
        if(sigsetjmp(jump,1)==0) { fn(mem,7);CHECK(0); }
        else {CHECK(signal_seen==SIGILL);CHECK(fault_pc==pc);}
    }
    puts("LRCPC2_FAULTS completed");fflush(stdout);
}
static void smc(void) {
    uint32_t *target=mmap(NULL,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(target!=MAP_FAILED);
    target[1]=0xd65f03c0u;
    op_fn store=make_op(2,0,1,0,0);
    for(unsigned i=0;i<200;i++) {
        store(target,0x52800000u|((i&1)<<5)); // stlur w0 updates MOV W0,#0/#1
        __builtin___clear_cache((char *)target,(char *)(target+2));
        CHECK(((uint64_t (*)(void))target)()==(i&1));
    }
    CHECK(munmap(target,4096)==0);
    puts("LRCPC2_SMC rewrites=200");fflush(stdout);
}
static uint64_t flag,payload;
static unsigned message_bad;
__attribute__((noinline)) static void release(uint64_t *p,uint64_t v) {
    register uint64_t *a __asm__("x0")=p;register uint64_t b __asm__("x1")=v;
    __asm__ volatile(".inst 0xd9000001"::"r"(a),"r"(b):"memory");
}
__attribute__((noinline)) static uint64_t acquire(uint64_t *p) {
    register uint64_t a __asm__("x0")=(uintptr_t)p;
    __asm__ volatile(".inst 0xd9400000":"+r"(a)::"memory");return a;
}
static void *writer(void *unused) {
    (void)unused;
    for(unsigned i=1;i<=10000;i++) {
        while(acquire(&flag)!=0) sched_yield();
        __atomic_store_n(&payload,(uint64_t)i,__ATOMIC_RELAXED);release(&flag,1);
    }
    return NULL;
}
static void messages(void) {
    pthread_t t;CHECK(pthread_create(&t,NULL,writer,NULL)==0);
    for(unsigned i=1;i<=10000;i++) {
        while(acquire(&flag)!=1) sched_yield();
        if(__atomic_load_n(&payload,__ATOMIC_RELAXED)!=i) message_bad++;
        release(&flag,0);
    }
    CHECK(pthread_join(t,NULL)==0);CHECK(message_bad==0);
    puts("LRCPC2_MESSAGES iterations=10000");fflush(stdout);
}
int main(void) {
    const size_t code_size=4*1024*1024;
    code=mmap(NULL,code_size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    unsigned char *mem=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(code==MAP_FAILED||mem==MAP_FAILED)return 2;
    values(mem);faults(mem);smc();messages();
    CHECK(used*4<code_size);
    printf("LRCPC2_PROBE %s checks=%u failures=%u code_bytes=%zu\n",failures?"FAIL":"PASS",checks,failures,used*4);
    return failures!=0;
}
