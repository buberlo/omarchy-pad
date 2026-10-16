/*
 * MAP_FIXED_NOREPLACE must be an exact placement that never replaces an existing mapping.
 *
 * Linux returns EEXIST and leaves the address space untouched when any page of the requested range
 * is already mapped. This emulator used to ignore the flag entirely: the call fell through to the
 * plain-hint path, returned a *different* address and reported success. Wine reserves the Windows
 * address space with exactly these probes, so it was told it owned ranges it did not, and its later
 * writes landed on the host heap -- glibc then aborted with
 * "malloc.c:2601 (sysmalloc): assertion failed" / "corrupted size vs. prev_size" inside wine and
 * wineserver, which is what stopped Valve's ARM64 Proton on the iPad.
 * See installation/linpad-steam/performance-2026-10-16/l4d-route/proton-arm64/README.md
 *
 * Build (the guest is Alpine/musl):
 *     zig cc -target aarch64-linux-musl -O1 map-fixed-noreplace-probe.c -o probe
 * Run it on the device; exit status 0 means the semantics are correct.
 *
 * Acceptance: this probe must print "ok" and exit 0. Before the fix it reported
 * "got=0x... errno=0 (unexpected; want MAP_FAILED/EEXIST)".
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

int main(void) {
    size_t page = (size_t) sysconf(_SC_PAGESIZE);
    size_t n = page * 8;
    char *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("base mmap"); return 1; }
    memset(p, 0xAA, n);
    printf("base=%p page=%zu\n", (void *) p, page);

    /* MAP_FIXED inside a live mapping replaces exactly that range and nothing else. */
    errno = 0;
    void *mid = mmap(p + 2 * page, 2 * page, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (mid != (void *) (p + 2 * page)) {
        printf("FAIL MAP_FIXED returned %p, wanted %p, errno=%d\n", mid, (void *) (p + 2 * page), errno);
        return 1;
    }
    size_t touched = 0;
    for (size_t i = 0; i < 2 * page; i++) if ((unsigned char) p[i] != 0xAA) touched++;
    for (size_t i = 4 * page; i < n; i++) if ((unsigned char) p[i] != 0xAA) touched++;
    if (touched != 0) {
        printf("FAIL MAP_FIXED changed %zu bytes outside its range\n", touched);
        return 1;
    }
    printf("MAP_FIXED mid: replaced exactly the requested range\n");

    /* The decisive one: a live page must make MAP_FIXED_NOREPLACE fail with EEXIST. */
    errno = 0;
    void *norepl = mmap(p, page, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (norepl != MAP_FAILED || errno != EEXIST) {
        printf("FAIL MAP_FIXED_NOREPLACE on a live page: got=%p errno=%d (%s; want MAP_FAILED/EEXIST)\n",
               norepl, errno, errno == EEXIST ? "EEXIST" : "unexpected");
        return 1;
    }
    printf("MAP_FIXED_NOREPLACE on a live page: refused with EEXIST\n");

    /* A genuinely free range must be granted, exactly where it was asked for. Punch a hole into
     * the mapping instead of reusing a live page -- asking for a live page is supposed to fail. */
    if (munmap(p + 5 * page, page) != 0) { perror("munmap hole"); return 1; }
    errno = 0;
    void *fresh = mmap(p + 5 * page, page, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (fresh != (void *) (p + 5 * page)) {
        printf("FAIL MAP_FIXED_NOREPLACE on a free page: got=%p wanted=%p errno=%d\n",
               fresh, (void *) (p + 5 * page), errno);
        return 1;
    }
    printf("MAP_FIXED_NOREPLACE on a free page: placed at %p\n", fresh);

    /* And it must not have disturbed anything outside its own range. Pages 2..4 were replaced by
     * the MAP_FIXED above, page 5 was the hole; pages 0, 1, 6 and 7 must still hold the pattern. */
    size_t disturbed = 0;
    for (size_t i = 0; i < 2 * page; i++) if ((unsigned char) p[i] != 0xAA) disturbed++;
    for (size_t i = 6 * page; i < n; i++) if ((unsigned char) p[i] != 0xAA) disturbed++;
    if (disturbed != 0) {
        printf("FAIL %zu bytes changed outside the requested ranges\n", disturbed);
        return 1;
    }

    /* MAP_FIXED together with NOREPLACE on a live range: measured on Linux (aarch64, Alpine) the
     * collision still reports EEXIST, so NOREPLACE keeps its meaning when FIXED is set too. */
    errno = 0;
    void *both = mmap(p, page, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_FIXED_NOREPLACE, -1, 0);
    if (both != MAP_FAILED || errno != EEXIST) {
        printf("FAIL MAP_FIXED|MAP_FIXED_NOREPLACE on a live page: got=%p errno=%d (want MAP_FAILED/EEXIST)\n",
               both, errno);
        return 1;
    }
    printf("MAP_FIXED|MAP_FIXED_NOREPLACE on a live page: refused with EEXIST\n");

    printf("ok\n");
    return 0;
}
