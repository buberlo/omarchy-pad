/* Does /proc/<pid>/mem write to exactly the address it was told?
 * Wine's server reads and patches other processes' memory through this file (libc's
 * process_vm_readv/writev are ENOSYS here), so an offset that lands somewhere else corrupts the
 * target process's heap -- which is exactly the shape of the wine crash. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define N (64 * 1024)
#define PAGE 4096

int main(void) {
    int up[2], down[2];
    if (pipe(up) || pipe(down)) { perror("pipe"); return 1; }
    pid_t pid = fork();
    if (pid == 0) {
        close(up[0]); close(down[1]);
        char *buf = malloc(N);
        memset(buf, 0xAA, N);
        if (write(up[1], &buf, sizeof buf) != sizeof buf) _exit(2);
        char go;
        if (read(down[0], &go, 1) != 1) _exit(3);
        /* Count deviations and where they are. */
        size_t off_target = 0, off_other = 0;
        for (size_t i = 0; i < N; i++) {
            if ((unsigned char) buf[i] == 0xAA) continue;
            if (i >= PAGE && i < 2 * PAGE) off_target++;
            else off_other++;
        }
        printf("child: bytes changed in the target page: %zu (want 4096)\n", off_target);
        printf("child: bytes changed ELSEWHERE: %zu (want 0)\n", off_other);
        printf("child: %s\n", (off_target == PAGE && off_other == 0) ? "ok" : "WRONG ADDRESS");
        _exit((off_target == PAGE && off_other == 0) ? 0 : 1);
    }
    close(up[1]); close(down[0]);
    char *child_buf = NULL;
    if (read(up[0], &child_buf, sizeof child_buf) != sizeof child_buf) { printf("no address\n"); return 1; }
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/mem", (int) pid);
    int fd = open(path, O_RDWR);
    if (fd < 0) { perror("open /proc/pid/mem"); return 1; }

    char *page = malloc(PAGE);
    memset(page, 0xBB, PAGE);
    ssize_t w = pwrite(fd, page, PAGE, (off_t) (child_buf + PAGE));
    printf("parent: wrote %zd bytes at %p + %d\n", w, (void *) child_buf, PAGE);

    /* Also read back through the same file: it must be what we just wrote. */
    char *back = malloc(PAGE);
    ssize_t r = pread(fd, back, PAGE, (off_t) (child_buf + PAGE));
    printf("parent: read back %zd bytes, first byte 0x%02x (want 0xbb)\n", r, (unsigned char) back[0]);

    if (write(down[1], "g", 1) != 1) { printf("signal failed\n"); return 1; }
    int status = 0;
    waitpid(pid, &status, 0);
    printf("parent: child exit=%d\n", WEXITSTATUS(status));
    return WEXITSTATUS(status);
}
