/* Does this emulator implement MAP_FIXED / MAP_FIXED_NOREPLACE the way Linux does?
 * A wrong answer here lets wine's address-space reservation overwrite live heap. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void) {
    size_t page = (size_t) sysconf(_SC_PAGESIZE);
    size_t n = page * 8;
    char *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("base mmap"); return 1; }
    memset(p, 0xAA, n);
    printf("base=%p page=%zu\n", (void *) p, page);

    errno = 0;
    void *mid = mmap(p + 2 * page, 2 * page, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    printf("MAP_FIXED mid: got=%p want=%p errno=%d\n", mid, (void *) (p + 2 * page), errno);
    size_t bad = 0;
    for (size_t i = 0; i < 2 * page; i++) if ((unsigned char) p[i] != 0xAA) bad++;
    for (size_t i = 4 * page; i < n; i++) if ((unsigned char) p[i] != 0xAA) bad++;
    printf("neighbour bytes changed by MAP_FIXED: %zu\n", bad);

    errno = 0;
    void *norepl = mmap(p, page, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    printf("MAP_FIXED_NOREPLACE on a live page: got=%p errno=%d (%s; want MAP_FAILED/EEXIST)\n",
           norepl, errno, errno == EEXIST ? "EEXIST" : "unexpected");

    errno = 0;
    void *hi = mmap((void *) 0x1000000000ULL, page, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    printf("MAP_FIXED at 64G: got=%p errno=%d\n", hi, errno);
    if (hi != MAP_FAILED) { memset(hi, 0x55, page); munmap(hi, page); }

    errno = 0;
    void *lo = mmap((void *) 0x10000, page, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    printf("MAP_FIXED low 64K: got=%p errno=%d (%s)\n", lo, errno,
           lo == MAP_FAILED ? "refused" : "granted");

    printf("summary: neighbours_bad=%zu\n", bad);
    return bad ? 1 : 0;
}
