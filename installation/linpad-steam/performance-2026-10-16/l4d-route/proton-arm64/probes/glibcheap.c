/* glibc's allocator uses brk for the main arena and mremap for large blocks.
 * musl uses mmap for everything, which is why the musl reproducer passed while wine did not. */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int check(const char *what, unsigned char *p, size_t n, unsigned char v) {
    for (size_t i = 0; i < n; i++) {
        if (p[i] != v) { printf("CORRUPT %s at offset %zu\n", what, i); return 1; }
    }
    return 0;
}

int main(void) {
    printf("glibc-heap probe: pid=%d\n", (int) getpid());
    fflush(stdout);
    int bad = 0;

    /* 1. small allocations growing the main arena through brk */
    for (int round = 0; round < 2000 && !bad; round++) {
        size_t n = 64 + (size_t) (round % 512);
        unsigned char *p = malloc(n);
        if (!p) { printf("FAIL malloc %zu\n", n); return 1; }
        memset(p, 0x5A, n);
        if (round % 64 == 0) free(p);
        else if (check("small", p, n, 0x5A)) bad = 1;
        else free(p);
    }
    printf("small brk-driven allocations: %s\n", bad ? "CORRUPT" : "ok");
    if (bad) return 1;

    /* 2. a block above the mmap threshold, then realloc across it (glibc uses mremap here) */
    size_t big_n = 64u << 20;
    unsigned char *big = malloc(big_n);
    if (!big) { printf("FAIL big malloc\n"); return 1; }
    memset(big, 0x11, big_n);
    if (check("big", big, big_n, 0x11)) return 1;
    size_t bigger_n = 96u << 20;
    unsigned char *bigger = realloc(big, bigger_n);
    if (!bigger) { printf("FAIL realloc across the mmap threshold\n"); return 1; }
    if (check("realloc prefix", bigger, big_n, 0x11)) return 1;
    printf("mmap threshold + realloc (mremap): ok\n");
    free(bigger);

    /* 3. free memory back to the system, then grow again -- malloc_trim uses brk shrinking */
    if (malloc_trim(0) != 0) printf("malloc_trim returned 1 (released memory)\n");
    unsigned char *again = malloc(32u << 20);
    if (!again) { printf("FAIL malloc after trim\n"); return 1; }
    memset(again, 0x22, 32u << 20);
    if (check("after trim", again, 32u << 20, 0x22)) return 1;
    printf("malloc_trim + regrow: ok\n");
    free(again);

    printf("summary: glibc heap ok\n");
    return 0;
}
