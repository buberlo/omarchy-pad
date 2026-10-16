/* Wine finds free address ranges by parsing /proc/self/maps. If the brk heap is missing from that
 * file, wine believes it is free and maps there with MAP_FIXED -- destroying glibc's heap, which is
 * exactly what the "corrupted size vs. prev_size" assertion reports. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    size_t n = 4u << 20;
    char *p = malloc(n);
    if (!p) { printf("malloc failed\n"); return 1; }
    memset(p, 0x5A, n);
    unsigned long heap_lo = (unsigned long) p, heap_hi = (unsigned long) p + n;
    unsigned long brk_now = (unsigned long) sbrk(0);
    printf("malloc block : 0x%lx .. 0x%lx\n", heap_lo, heap_hi);
    printf("sbrk(0)      : 0x%lx\n", brk_now);

    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) { perror("maps"); return 1; }
    char line[512];
    int covering = 0, heap_labelled = 0, ranges = 0;
    unsigned long first_lo = 0, last_hi = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long lo = 0, hi = 0;
        if (sscanf(line, "%lx-%lx", &lo, &hi) != 2) continue;
        ranges++;
        if (!first_lo) first_lo = lo;
        last_hi = hi;
        if (strstr(line, "[heap]")) heap_labelled = 1;
        if (lo <= heap_lo && hi >= heap_hi) covering = 1;
    }
    fclose(f);
    printf("maps ranges  : %d (0x%lx .. 0x%lx)\n", ranges, first_lo, last_hi);
    printf("a maps entry covers the malloc block : %s\n", covering ? "yes" : "NO -- wine would think it is free");
    printf("[heap] labelled                      : %s\n", heap_labelled ? "yes" : "no");
    printf("%s\n", covering ? "ok" : "FAIL");
    return covering ? 0 : 1;
}
