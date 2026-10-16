/* glibc grows its heap with brk and writes only what it uses. If the emulator records just the
 * touched pages instead of the whole grown range, a later mmap can be placed on an untouched heap
 * page; the first allocation that uses that page then shares it with whatever lives there.
 * On Linux the whole [start_brk, brk) range is mapped, so this must not happen. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define GROW (8u << 20)
#define BLOCK (1u << 20)

int main(void) {
    char *start = sbrk(0);
    if (start == (void *) -1) { perror("sbrk"); return 1; }
    printf("heap start            : %p\n", (void *) start);

    /* grow the heap without touching the new pages */
    if (brk(start + GROW) != 0) { perror("brk grow"); return 1; }
    char *end = sbrk(0);
    printf("heap end after brk    : %p (%u MB grown, untouched)\n", (void *) end, GROW >> 20);

    /* a fresh anonymous mapping must not be placed inside [start, end) */
    char *m = mmap(NULL, BLOCK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) { perror("mmap"); return 1; }
    int overlap = (m < end) && (m + BLOCK > start);
    printf("fresh mmap            : %p .. %p\n", (void *) m, (void *) (m + BLOCK));
    printf("inside the grown heap : %s\n", overlap ? "YES -- the untouched heap is not reserved" : "no");

    if (overlap) {
        memset(m, 0x5A, BLOCK);
        /* now use the heap the way malloc would, and see whether the mapping survives */
        for (size_t off = 0; off < GROW; off += 4096)
            start[off] = (char) (off >> 12);
        size_t damaged = 0;
        for (size_t i = 0; i < BLOCK; i++) if ((unsigned char) m[i] != 0x5A) damaged++;
        printf("mapping bytes damaged by heap use: %zu\n", damaged);
    }
    printf("%s\n", overlap ? "FAIL" : "ok");
    return overlap ? 1 : 0;
}
