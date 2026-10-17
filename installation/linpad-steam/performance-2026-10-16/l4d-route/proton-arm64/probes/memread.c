#define _GNU_SOURCE
/* Read another process's memory through /proc/<pid>/mem with pread() -- dd's lseek fails on this
 * filesystem, pread does not. Used to look at what a wine client actually sees in the shared session
 * block the wineserver handed it. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s PID ADDR LEN [ADDR LEN ...]\n", argv[0]); return 64; }
    char path[64];
    snprintf(path, sizeof path, "/proc/%s/mem", argv[1]);
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return 1; }
    for (int i = 2; i + 1 < argc; i += 2) {
        unsigned long addr = strtoul(argv[i], NULL, 0);
        long len = strtol(argv[i + 1], NULL, 0);
        unsigned char buf[256];
        if (len > (long) sizeof buf) len = sizeof buf;
        ssize_t n = pread(fd, buf, len, (off_t) addr);
        printf("%#lx (%ld bytes): ", addr, len);
        if (n <= 0) { printf("pread returned %zd (%s)\n", n, strerror(errno)); continue; }
        for (ssize_t k = 0; k < n; k++) printf("%02x%s", buf[k], (k % 8 == 7) ? " " : "");
        printf("\n");
    }
    close(fd);
    return 0;
}
