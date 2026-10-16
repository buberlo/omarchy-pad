#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* Diagnostic boundary around glibc calls that reach LinPad's VFS. */
static void record(const char *name, int dirfd, const char *path, int mode, int flags) {
    char line[4608], target[4096] = "", proc[80];
    if (dirfd != AT_FDCWD) {
        snprintf(proc, sizeof(proc), "/proc/self/fd/%d", dirfd);
        ssize_t n = readlink(proc, target, sizeof(target) - 1);
        if (n >= 0) target[n] = 0;
    }
    int n = snprintf(line, sizeof(line), "%s fd=%d target=%s path=%s mode=%x flags=%x\n",
                     name, dirfd, target, path, mode, flags);
    int fd = open("/tmp/steam-access.log", O_WRONLY | O_APPEND | O_CREAT, 0600);
    if (fd >= 0) { write(fd, line, n < (int)sizeof(line) ? n : sizeof(line)-1); close(fd); }
}

int faccessat(int fd, const char *path, int mode, int flags) {
    record("faccessat", fd, path, mode, flags);
    /* A relative lookup on a non-directory must fail with ENOTDIR, rather
     * than feeding an anonymous-inode display name to path_normalize. */
    if (fd != AT_FDCWD && path[0] && path[0] != '/') {
        struct stat st;
        if (fstat(fd, &st)) return -1;
        if (!S_ISDIR(st.st_mode)) { errno = ENOTDIR; return -1; }
    }
    static int (*original)(int, const char *, int, int);
    if (!original) original = dlsym(RTLD_NEXT, "faccessat");
    return original(fd, path, mode, flags);
}

int access(const char *path, int mode) {
    record("access", AT_FDCWD, path, mode, 0);
    static int (*original)(const char *, int);
    if (!original) original = dlsym(RTLD_NEXT, "access");
    return original(path, mode);
}
