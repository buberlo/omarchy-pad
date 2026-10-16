/* Does /proc/self/maps list the brk heap? Wine parses that file to find free address ranges; a heap
 * that is missing there looks free, and wine's MAP_FIXED placements then land in glibc's heap. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    /* small allocations (<= 128 KB) come from the brk heap, unlike big ones which use mmap */
    for (int i = 0; i < 4000; i++) {
        char *p = malloc(64 * 1024);
        if (!p) { printf("malloc failed at %d\n", i); break; }
        memset(p, 0x5A, 64 * 1024);
    }
    unsigned long brk_now = (unsigned long) sbrk(0);
    printf("brk (heap end) : 0x%lx\n", brk_now);

    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) { perror("maps"); return 1; }
    char line[512];
    unsigned long low_lo = 0, low_hi = 0;
    int entries_below_256mb = 0, covers_brk = 0;
    printf("entries below 256 MB:\n");
    while (fgets(line, sizeof line, f)) {
        unsigned long lo = 0, hi = 0;
        if (sscanf(line, "%lx-%lx", &lo, &hi) != 2) continue;
        if (lo < 0x10000000UL) {
            entries_below_256mb++;
            if (entries_below_256mb <= 6) printf("  %s", line);
            if (!low_lo) low_lo = lo;
            if (hi > low_hi) low_hi = hi;
        }
        if (lo <= brk_now && hi >= brk_now) covers_brk = 1;
    }
    fclose(f);
    printf("entries below 256 MB: %d, covering up to 0x%lx\n", entries_below_256mb, low_hi);
    printf("brk (0x%lx) covered by a maps entry: %s\n", brk_now, covers_brk ? "yes" : "NO");
    /* the heap start is the end of the image's data segment; report the gap */
    printf("%s\n", covers_brk ? "ok" : "FAIL: the brk heap is invisible in maps");
    return covers_brk ? 0 : 1;
}
