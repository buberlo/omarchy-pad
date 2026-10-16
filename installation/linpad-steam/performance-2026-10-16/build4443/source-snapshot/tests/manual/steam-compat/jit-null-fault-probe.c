#define _GNU_SOURCE
#include <signal.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

// An invalid TLB tag is 1. Previously range arithmetic mistook addresses
// near zero for hits, causing host faults and skipping the guest handler.
static sigjmp_buf resume;
static volatile sig_atomic_t seen, wrong_signal;
static uintptr_t expected_address;

static void on_segv(int sig, siginfo_t *info, void *context) {
    (void)context;
    seen++;
    if (sig != SIGSEGV || (uintptr_t)info->si_addr != expected_address)
        wrong_signal = 1;
    siglongjmp(resume, 1);
}

#define ACCESS(name, instruction, ...) \
    __attribute__((noinline)) static void name(void *address) { \
        __asm__ volatile("mov x1,%0\n\t" instruction :: "r"(address) : "x1", __VA_ARGS__, "memory"); \
    }
ACCESS(read1, "ldrb w0,[x1]", "x0")
ACCESS(read2, "ldrh w0,[x1]", "x0")
ACCESS(read4, "ldr w0,[x1]", "x0")
ACCESS(read8, "ldr x1,[x1]", "x0") // Same instruction as FEX's SIGSEGV trampoline.
ACCESS(read16, "ldr q0,[x1]", "v0")
ACCESS(read32, "ldp q0,q1,[x1]", "v0", "v1")
ACCESS(write1, "strb wzr,[x1]", "x0")
ACCESS(write2, "strh wzr,[x1]", "x0")
ACCESS(write4, "str wzr,[x1]", "x0")
ACCESS(write8, "str xzr,[x1]", "x0")
ACCESS(write16, "movi v0.16b,#0\n\tstr q0,[x1]", "v0")
ACCESS(write32, "movi v0.16b,#0\n\tmovi v1.16b,#0\n\tstp q0,q1,[x1]", "v0", "v1")

int main(void) {
    alarm(10);
    struct sigaction sa = {.sa_sigaction = on_segv, .sa_flags = SA_SIGINFO};
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL)) return 2;
    const struct { const char *name; void (*access)(void *); } cases[] = {
        {"read1",read1},{"read2",read2},{"read4",read4},{"read8",read8},
        {"read16",read16},{"read32",read32},{"write1",write1},
        {"write2",write2},{"write4",write4},{"write8",write8},
        {"write16",write16},{"write32",write32},
    };
    // Keep valid same-page and page-crossing accesses working in each path.
    unsigned char *memory = mmap(NULL, 8192, PROT_READ|PROT_WRITE,
                                 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) return 2;
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        cases[i].access(memory + 64);
        cases[i].access(memory + 4095);
        for (uintptr_t address = 0; address <= 1; address++) {
            expected_address = address;
            sig_atomic_t before = seen;
            if (sigsetjmp(resume, 1) == 0) {
                cases[i].access((void *)address);
                fprintf(stderr, "FAIL %s at %#lx returned without SIGSEGV\n",
                        cases[i].name, (unsigned long)address);
                return 1;
            }
            if (wrong_signal || seen != before + 1) {
                fprintf(stderr, "FAIL %s signal/address\n", cases[i].name);
                return 1;
            }
        }
    }
    munmap(memory, 8192);
    printf("PASS 24 exact-address SIGSEGV deliveries; valid and crossing accesses\n");
    return 0;
}
