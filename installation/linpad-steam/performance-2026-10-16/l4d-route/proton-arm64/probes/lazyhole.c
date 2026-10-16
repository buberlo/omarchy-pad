/* Is a lazily-mapped large reservation visible to MAP_FIXED_NOREPLACE?
 * Wine reserves about 1.66 GB of PROT_NONE (MAP_NORESERVE|MAP_FIXED) and then places DLLs inside
 * that range with MAP_FIXED_NOREPLACE. If the reservation only lives as a "reservation" and not in
 * the page table, the NOREPLACE check sees a hole, the caller maps there, and the first
 * demand-mapped touch of the reservation (mem_find_reservation on the write fault) maps over it.
 * That is the shape of the wine crash. On Linux the reservation is a real mapping and NOREPLACE
 * must fail with EEXIST. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

int main(void) {
    size_t res = 512UL << 20; /* past the emulator's 256 MB lazy threshold */
    char *r = mmap(NULL, res, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (r == MAP_FAILED) { perror("reserve"); return 1; }
    printf("reservation: %p .. %p (PROT_NONE, NORESERVE, %zu MB)\n",
           (void *) r, (void *) (r + res), res >> 20);

    /* 1. NOREPLACE inside the reservation: Linux says EEXIST. */
    char *slot = r + (16UL << 20);
    errno = 0;
    void *placed = mmap(slot, 1UL << 20, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    int correctly_refused = (placed == MAP_FAILED && errno == EEXIST);
    printf("NOREPLACE inside the reservation: got=%p errno=%d (%s)\n", placed, errno,
           correctly_refused ? "correct: EEXIST" : "WRONG: the reservation looks like a hole");

    /* 2. Even if it was granted, does using the reservation destroy that memory? */
    if (placed != MAP_FAILED) {
        memset(placed, 0x5C, 1UL << 20);
        /* touch a neighbouring page of the reservation the way wine would (mprotect + write) */
        char *other = r + (17UL << 20);
        if (mprotect(other, 4096, PROT_READ | PROT_WRITE) == 0) memset(other, 0x11, 4096);
        size_t damaged = 0;
        for (size_t i = 0; i < (1UL << 20); i++) if (((unsigned char *) placed)[i] != 0x5C) damaged++;
        printf("bytes of the placement damaged by using the reservation: %zu\n", damaged);
    }
    printf("%s\n", correctly_refused ? "ok" : "FAIL");
    return correctly_refused ? 0 : 1;
}
