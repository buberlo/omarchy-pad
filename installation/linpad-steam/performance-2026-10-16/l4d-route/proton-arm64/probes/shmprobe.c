/* Wine shares structures between its server and every client process, through a file-backed
 * MAP_SHARED mapping (historically /dev/shm, otherwise a file in the prefix or a memfd).
 * If this emulator maps shared memory per process instead of sharing the pages, each wine process
 * sees different contents -- which would explain wild pointers, heap corruption and identical
 * crashes in several processes at once, independent of layout and of FEX. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/syscall.h>
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
static int memfd_create_(const char *name, unsigned flags) { return (int) syscall(SYS_memfd_create, name, flags); }

#define N 4096

static int check(const char *what, char *p) {
    for (int i = 0; i < N; i++) {
        if (p[i] != (char) (i & 0xff)) {
            printf("  %-22s MISMATCH at %d: got 0x%02x want 0x%02x\n", what, i,
                   (unsigned char) p[i], (unsigned char) (i & 0xff));
            return 1;
        }
    }
    printf("  %-22s shared writes visible\n", what);
    return 0;
}

static int test_mapping(const char *what, char *p) {
    memset(p, 0, N);
    pid_t pid = fork();
    if (pid == 0) {
        for (int i = 0; i < N; i++) p[i] = (char) (i & 0xff);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return check(what, p);
}

int main(void) {
    printf("shared-memory probe\n");
    int bad = 0;

    /* 1. anonymous shared memory (MAP_SHARED|MAP_ANONYMOUS) */
    char *anon = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (anon == MAP_FAILED) { perror("anon shared mmap"); bad++; }
    else bad += test_mapping("anon MAP_SHARED", anon);

    /* 2. memfd + MAP_SHARED (what modern wine uses) */
    int fd = memfd_create_("probe", 0);
    if (fd < 0) { printf("  memfd_create failed: %s\n", strerror(errno)); bad++; }
    else {
        if (ftruncate(fd, N)) perror("ftruncate");
        char *m = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (m == MAP_FAILED) { perror("memfd mmap"); bad++; }
        else bad += test_mapping("memfd MAP_SHARED", m);
        close(fd);
    }

    /* 3. /dev/shm file + MAP_SHARED (wine's classic path) */
    const char *path = "/dev/shm/linpad-shm-probe";
    int f2 = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (f2 < 0) { printf("  open %s failed: %s\n", path, strerror(errno)); bad++; }
    else {
        if (ftruncate(f2, N)) perror("ftruncate");
        char *m2 = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED, f2, 0);
        if (m2 == MAP_FAILED) { perror("dev/shm mmap"); bad++; }
        else bad += test_mapping("/dev/shm MAP_SHARED", m2);
        close(f2);
        unlink(path);
    }

    /* 4. a file in /tmp + MAP_SHARED (the fallback wine uses when /dev/shm is absent) */
    const char *tpath = "/tmp/linpad-shm-probe";
    int f3 = open(tpath, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (f3 < 0) { printf("  open %s failed: %s\n", tpath, strerror(errno)); bad++; }
    else {
        if (ftruncate(f3, N)) perror("ftruncate");
        char *m3 = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED, f3, 0);
        if (m3 == MAP_FAILED) { perror("tmp file mmap"); bad++; }
        else bad += test_mapping("/tmp file MAP_SHARED", m3);
        close(f3);
        unlink(tpath);
    }

    printf("summary: %s\n", bad ? "SHARED MEMORY IS BROKEN" : "shared memory ok");
    return bad ? 1 : 0;
}
