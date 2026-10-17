/* Chromium/CEF and modern wine create their shared-memory objects as sealed memfds
 * (memfd_create + ftruncate + F_ADD_SEALS with F_SEAL_SHRINK/GROW/SEAL), and wine keeps its
 * window-station session objects in such a section. If sealing is unsupported -- or if /dev/shm
 * cannot hold a real allocation -- both fail exactly the way they do here. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif
#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#define F_GET_SEALS 1034
#define F_SEAL_SEAL 0x0001
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW 0x0004
#define F_SEAL_WRITE 0x0008
#endif

#define BIG (32u << 20)

int main(void) {
    int bad = 0;
    printf("=== memfd sealing ===\n");
    int fd = (int) syscall(SYS_memfd_create, "caps-probe", MFD_ALLOW_SEALING);
    if (fd < 0) { printf("memfd_create(ALLOW_SEALING) failed: %s\n", strerror(errno)); bad++; }
    else {
        if (ftruncate(fd, BIG)) { printf("ftruncate to %u MB failed: %s\n", BIG >> 20, strerror(errno)); bad++; }
        char *m = mmap(NULL, BIG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (m == MAP_FAILED) { printf("mmap of %u MB failed: %s\n", BIG >> 20, strerror(errno)); bad++; }
        else { memset(m, 0x5A, BIG); printf("mapped and touched %u MB through a memfd: ok\n", BIG >> 20); }
        errno = 0;
        int seals = F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL;
        if (fcntl(fd, F_ADD_SEALS, seals) != 0) {
            printf("F_ADD_SEALS(0x%x) failed: %s\n", seals, strerror(errno)); bad++;
        } else {
            int got = fcntl(fd, F_GET_SEALS);
            printf("seals applied: 0x%x (asked 0x%x)\n", got, seals);
            if ((got & seals) != seals) { printf("seals not all reported back\n"); bad++; }
            /* a sealed memfd must refuse to shrink */
            errno = 0;
            if (ftruncate(fd, 4096) == 0) { printf("sealed memfd accepted a shrink (should fail)\n"); bad++; }
            else printf("sealed memfd refused to shrink: %s\n", strerror(errno));
        }
    }

    printf("=== /dev/shm capacity ===\n");
    const char *path = "/dev/shm/caps-probe";
    int f2 = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (f2 < 0) { printf("open %s failed: %s\n", path, strerror(errno)); bad++; }
    else {
        if (ftruncate(f2, BIG)) { printf("ftruncate %u MB in /dev/shm failed: %s\n", BIG >> 20, strerror(errno)); bad++; }
        char *p = mmap(NULL, BIG, PROT_READ | PROT_WRITE, MAP_SHARED, f2, 0);
        if (p == MAP_FAILED) { printf("mapping %u MB from /dev/shm failed: %s\n", BIG >> 20, strerror(errno)); bad++; }
        else { memset(p, 0x3C, BIG); printf("wrote %u MB into /dev/shm: ok\n", BIG >> 20); }
        struct stat st;
        if (fstat(f2, &st) == 0) printf("/dev/shm file size after write: %lld bytes\n", (long long) st.st_size);
        close(f2);
        unlink(path);
    }
    printf("summary: %s\n", bad ? "SHARED-MEMORY CAPABILITY MISSING" : "shared-memory capabilities ok");
    return bad ? 1 : 0;
}
