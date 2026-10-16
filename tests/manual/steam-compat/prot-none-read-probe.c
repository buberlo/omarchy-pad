#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static sigjmp_buf resume;
static volatile sig_atomic_t caught;

static void on_segv(int signal_number, siginfo_t *info, void *context) {
    (void)context;
    if (signal_number == SIGSEGV && info->si_addr == NULL) caught = 1;
    siglongjmp(resume, 1);
}

// FEX uses x29 for its own state, so a kernel diagnostic must not assume it
// points to a readable frame chain. Recover via siglongjmp, which restores it.
__attribute__((naked)) static void null_fault_with_guard_fp(void *guard) {
    __asm__ volatile("mov x29,x0\n\tmov x1,#0\n\tldr x1,[x1]\n\tret");
}

static int expect_read(int fds[2], void *address, int readable) {
    errno = 0;
    ssize_t n = write(fds[1], address, 1);
    if (!readable) {
        if (n == -1 && errno == EFAULT) return 0;
        fprintf(stderr, "FAIL protected buffer write=%zd errno=%d\n", n, errno);
        return 1;
    }
    unsigned char value;
    if (n == 1 && read(fds[0], &value, 1) == 1) return 0;
    fprintf(stderr, "FAIL accessible buffer write=%zd errno=%d\n", n, errno);
    return 1;
}

int main(int argc, char **argv) {
    alarm(10);
    int fds[2];
    if (pipe(fds)) return 2;
    int fault_test = argc > 1 && !strncmp(argv[1], "fault", 5);
    size_t length = argc > 1 && !strcmp(argv[1], "fault-lazy") ? 128*1024*1024 : 16384;
    unsigned char *p = mmap(NULL, length, PROT_NONE,
                            MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return 2;

    if (fault_test) {
        struct sigaction sa = {.sa_sigaction = on_segv, .sa_flags = SA_SIGINFO};
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGSEGV, &sa, NULL)) return 2;
        if (sigsetjmp(resume, 1) == 0) null_fault_with_guard_fp(p);
        if (!caught) { fputs("FAIL guest fault not delivered\n", stderr); return 1; }
        puts("PASS null fault with PROT_NONE frame pointer reaches guest handler");
    } else {
        // Initially the backing host page itself is PROT_NONE.
        if (expect_read(fds, p, 0)) return 1;
        if (mprotect(p, 4096, PROT_READ|PROT_WRITE)) return 2;
        p[0] = 17;
        if (expect_read(fds, p, 1)) return 1;
        // These guest pages share a 16 KiB host page. Promoting p must not
        // accidentally make the other PROT_NONE guest pages readable.
        if (expect_read(fds, p + 4096, 0) || expect_read(fds, p + 8192, 0)) return 1;
        if (mprotect(p, 4096, PROT_NONE)) return 2;
        if (expect_read(fds, p, 0)) return 1;
        if (mprotect(p, 4096, PROT_READ)) return 2;
        if (expect_read(fds, p, 1)) return 1;
        if (mprotect(p, 4096, PROT_READ|PROT_WRITE)) return 2;
        p[0] = 31;
        if (expect_read(fds, p, 1)) return 1;
        // Large mappings are reservation-only until protection is raised.
        unsigned char *lazy = mmap(NULL, 128*1024*1024, PROT_NONE,
                                    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (lazy == MAP_FAILED) return 2;
        if (expect_read(fds, lazy, 0)) return 1;
        if (mprotect(lazy, 4096, PROT_READ|PROT_WRITE)) return 2;
        lazy[0] = 42;
        if (expect_read(fds, lazy, 1) || expect_read(fds, lazy + 4096, 0)) return 1;
        munmap(lazy, 128*1024*1024);
        puts("PASS syscall EFAULT for PROT_NONE; readable pages and transitions preserved");
    }
    munmap(p, length);
    close(fds[0]); close(fds[1]);
    return 0;
}
