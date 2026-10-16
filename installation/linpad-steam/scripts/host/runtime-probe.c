#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

static int failures;
static void check_access(const char *label, int fd, const char *path, int expected) {
    errno = 0;
    long result = syscall(SYS_faccessat, fd, path, F_OK, 0);
    int actual = result == 0 ? 0 : errno;
    printf("%s: result=%ld errno=%d expected=%d %s\n", label, result, actual,
           expected, actual == expected ? "PASS" : "FAIL");
    failures += actual != expected;
}

int main(void) {
    unsigned long midr;
    __asm__ volatile("mrs %0, MIDR_EL1" : "=r"(midr));
    printf("MIDR_EL1 read succeeded: %#lx\n", midr);
    int pipes[2];
    if (pipe(pipes)) return 90;
    int file = open("/opt/fex/hello-x86_64", O_RDONLY);
    int dir = open("/opt/fex", O_RDONLY | O_DIRECTORY);
    if (file < 0 || dir < 0) return 91;
    check_access("pipe-relative", pipes[0], "child", ENOTDIR);
    check_access("file-relative", file, "child", ENOTDIR);
    check_access("directory-relative", dir, "hello-x86_64", 0);
    check_access("absolute-ignores-pipe", pipes[0], "/opt/fex/hello-x86_64", 0);
    check_access("empty-path", pipes[0], "", ENOENT);
    close(dir); close(file); close(pipes[0]); close(pipes[1]);
    return failures;
}
