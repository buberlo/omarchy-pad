/* Are lazily-mapped large PROT_NONE reservations visible to the address-space allocator?
 * Wine reserves gigabytes of PROT_NONE for the Windows address space. If such a reservation is
 * only recorded as a "reservation" and not in the page table, a later plain mmap(NULL, ...) --
 * which is how glibc allocates large blocks and thread stacks -- can be placed inside it, and the
 * first demand-mapped touch of the reservation then destroys that memory. That is the shape of the
 * wine crash ("corrupted size vs. prev_size"), so this probe checks it directly. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void) {
    size_t res = 1UL << 30; /* 1 GB: past the emulator's 128 MB / 256 MB lazy thresholds */
    char *r = mmap(NULL, res, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (r == MAP_FAILED) { perror("reserve"); return 1; }
    printf("reservation: %p .. %p\n", (void *) r, (void *) (r + res));

    size_t n = 1UL << 20;
    char *a = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (a == MAP_FAILED) { perror("anon"); return 1; }
    printf("anon block : %p .. %p\n", (void *) a, (void *) (a + n));

    int inside = (a < r + res) && (a + n > r);
    printf("anon block inside the reservation: %s\n",
           inside ? "YES -- the reservation is invisible to the allocator" : "no");

    memset(a, 0x77, n);

    /* Use part of the reservation the way wine does: make one page writable and touch it. */
    char *sub = r + 0x1000;
    if (mprotect(sub, 4096, PROT_READ | PROT_WRITE) != 0) perror("mprotect sub");
    memset(sub, 0x88, 4096);

    size_t damaged = 0;
    for (size_t i = 0; i < n; i++) if ((unsigned char) a[i] != 0x77) damaged++;
    printf("anon bytes damaged by using the reservation: %zu\n", damaged);

    int ok = !inside && damaged == 0;
    printf("%s\n", ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
