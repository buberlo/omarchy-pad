/* Does the emulator's address-space bookkeeping know about the brk heap?
 * If it does not, a MAP_FIXED_NOREPLACE probe that lands inside glibc's own heap is answered
 * "free", the caller maps there, and the allocator's metadata is destroyed underneath it --
 * which is exactly the shape of the wine crash ("corrupted size vs. prev_size"). */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void) {
    /* Grow the brk region explicitly: musl's malloc uses mmap, so without this the heap segment
     * can stay empty and the probe would test nothing. */
    if (sbrk(256 * 1024) == (void *) -1) printf("sbrk growth failed\n");
    for (int i = 0; i < 200; i++) {
        void *p = malloc(64 * 1024);
        if (p) memset(p, 0xA5, 64 * 1024);
    }
    void *brk_now = sbrk(0);
    printf("sbrk(0)=%p\n", brk_now);

    unsigned long hs = 0, he = 0;
    int heap_listed = 0;
    FILE *f = fopen("/proc/self/maps", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            if (strstr(line, "[heap]")) { sscanf(line, "%lx-%lx", &hs, &he); heap_listed = 1; break; }
        }
        fclose(f);
    }
    printf("maps [heap]: %s (0x%lx-0x%lx)\n", heap_listed ? "yes" : "NO", hs, he);

    unsigned long probe = heap_listed ? (he - 4096) : ((unsigned long) brk_now - 8192);
    errno = 0;
    void *r = mmap((void *) probe, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    int correct = (r == MAP_FAILED && errno == EEXIST);
    printf("MAP_FIXED_NOREPLACE inside the heap at 0x%lx: got=%p errno=%d -- %s\n",
           probe, r, errno, correct ? "correct (EEXIST)" : "WRONG, the heap looks free");

    /* And is a page that malloc itself handed out also "free"? */
    char *live = malloc(4096);
    if (live) {
        memset(live, 0x33, 4096);
        unsigned long a = (unsigned long) live & ~0xFFFUL;
        errno = 0;
        void *r2 = mmap((void *) a, 4096, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        int ok2 = (r2 == MAP_FAILED && errno == EEXIST);
        printf("MAP_FIXED_NOREPLACE over a live malloc block at 0x%lx: got=%p errno=%d -- %s\n",
               a, r2, errno, ok2 ? "correct (EEXIST)" : "WRONG");
        return correct && ok2 ? 0 : 1;
    }
    return correct ? 0 : 1;
}
