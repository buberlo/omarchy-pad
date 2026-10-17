/* Wine reserves its ARM64EC map near the top of the 47-bit address space
 * (err:virtual:try_map_free_area mmap() error Cannot allocate memory, range 0x7ffefe000000-0x7ffffe000000)
 * and this emulator answers ENOMEM there. Reproduce it with the same flag combinations wine uses and
 * find which one fails. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0x4000
#endif

#define GB (1u << 30)
#define MAXGB 8
static const unsigned long hints[] = { 0x7ffefe000000UL, 0x7ffefe000000UL + 2 * GB, 0x6ffefc5a0000UL };

static void try_n(const char *what, unsigned long hint, int prot, int extra, unsigned long size) {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | extra;
    errno = 0;
    void *p = mmap((void *) hint, size, prot, flags, -1, 0);
    if (p == MAP_FAILED)
        printf("  %-52s FAILED errno=%d (%s)\n", what, errno, strerror(errno));
    else {
        printf("  %-52s ok at %p (hint %#lx)\n", what, p, hint);
        munmap(p, size);
    }
}

static void try(const char *what, unsigned long hint, int prot, int extra) {
    try_n(what, hint, prot, extra, GB);
}

int main(int argc, char **argv) {
    printf("page size %ld\n", sysconf(_SC_PAGESIZE));
    unsigned long gb = argc > 1 ? (unsigned long) atoi(argv[1]) : 1;
    char label[96];
    for (unsigned long g = 1; g <= gb && g <= MAXGB; g++) {
        snprintf(label, sizeof label, "%lu GB, no hint, PROT_READ|NORESERVE", g);
        try_n(label, 0, PROT_READ, MAP_NORESERVE, g * GB);
        snprintf(label, sizeof label, "%lu GB, hint 0x7ffefe000000, PROT_READ|NORESERVE", g);
        try_n(label, 0x7ffefe000000UL, PROT_READ, MAP_NORESERVE, g * GB);
        snprintf(label, sizeof label, "%lu GB, hint, PROT_READ (no NORESERVE)", g);
        try_n(label, 0x7ffefe000000UL, PROT_READ, 0, g * GB);
        snprintf(label, sizeof label, "%lu GB, hint, PROT_NONE|NORESERVE", g);
        try_n(label, 0x7ffefe000000UL, PROT_NONE, MAP_NORESERVE, g * GB);
    }
    return 0;
    try("no hint, PROT_READ|NORESERVE", 0, PROT_READ, MAP_NORESERVE);
    try("no hint, PROT_READ", 0, PROT_READ, 0);
    try("hint 0x7ffefe000000, PROT_READ", hints[0], PROT_READ, 0);
    try("hint 0x7ffefe000000, PROT_READ|NORESERVE", hints[0], PROT_READ, MAP_NORESERVE);
    try("hint 0x7ffefe000000, PROT_NONE|NORESERVE", hints[0], PROT_NONE, MAP_NORESERVE);
    try("hint 0x7ffefe000000, PROT_READ|NORESERVE|NOREPLACE", hints[0], PROT_READ, MAP_NORESERVE | MAP_FIXED_NOREPLACE);
    try("hint 0x7ffefe000000, PROT_READ|NOREPLACE", hints[0], PROT_READ, MAP_FIXED_NOREPLACE);
    try("hint +2GB, PROT_READ", hints[1], PROT_READ, 0);
    try("hint 0x6ffefc5a0000, PROT_READ", hints[2], PROT_READ, 0);
    /* and a small mapping at the same height, to separate size from address */
    errno = 0;
    void *small = mmap((void *) hints[0], 64 * 1024, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("  %-52s %s\n", "hint 0x7ffefe000000, 64 KB",
           small == MAP_FAILED ? "FAILED" : "ok");
    if (small != MAP_FAILED) munmap(small, 64 * 1024);
    return 0;
}
