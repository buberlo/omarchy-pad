#define _GNU_SOURCE
/* wine maps a shared section at a non-zero offset:
 *   map_shared_session_block(): LARGE_INTEGER off = { offset - offset % AllocationGranularity };
 *                               NtMapViewOfSection( handle, ..., &off, &block->size, ... )
 * and then reads the object at  block->data + offset - block->offset . If this filesystem ignores the
 * requested file offset and always maps from the start, every client reads the wrong part of the
 * section -- exactly the observed "Session object id doesn't match expected id". */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define PAGE 4096
#define PAGES 32

int main(void) {
    const char *path = "/tmp/offset-map-probe";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { perror("open"); return 1; }
    if (ftruncate(fd, (off_t) PAGE * PAGES)) { perror("ftruncate"); return 1; }
    /* one recognisable value per page */
    unsigned char *whole = mmap(NULL, (size_t) PAGE * PAGES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (whole == MAP_FAILED) { perror("mmap whole"); return 1; }
    for (int p = 0; p < PAGES; p++) memset(whole + (size_t) p * PAGE, 0xA0 + p, PAGE);
    msync(whole, (size_t) PAGE * PAGES, MS_SYNC);

    int bad = 0;
    const int pages[] = { 0, 1, 3, 8, 16, 31 };
    for (unsigned pi = 0; pi < sizeof(pages) / sizeof(pages[0]); pi++) {
        int p = pages[pi];
        off_t off = (off_t) p * PAGE;
        unsigned char *m = mmap(NULL, PAGE, PROT_READ, MAP_SHARED, fd, off);
        if (m == MAP_FAILED) { printf("  offset %6ld: mmap failed\n", (long) off); bad++; continue; }
        unsigned char want = (unsigned char) (0xA0 + p);
        int ok = (m[0] == want && m[PAGE - 1] == want);
        printf("  offset %6ld (page %2d): first byte 0x%02x last 0x%02x, want 0x%02x -- %s\n",
               (long) off, p, m[0], m[PAGE - 1], want, ok ? "ok" : "WRONG PAGE MAPPED");
        if (!ok) bad++;
        munmap(m, PAGE);
    }
    munmap(whole, (size_t) PAGE * PAGES);
    close(fd);
    unlink(path);
    printf("%s\n", bad ? "FAIL: file offsets in mmap are not honoured" : "ok");
    return bad ? 1 : 0;
}
