/* Wine identifies files (and thus shared mappings) by st_dev and st_ino -- the server call
 * "is_same_mapping" compares them, and it answered NOT_SAME_DEVICE thousands of times during a
 * single wine startup. If this filesystem reports different ids for the same file, wine can never
 * recognise a mapping as shared: every process maps its own anonymous copy, and the "shared session
 * object" of a window class does not exist in the client. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

static void ids(const char *what, const char *path) {
    struct stat st;
    if (stat(path, &st)) { printf("  %-28s stat failed\n", what); return; }
    printf("  %-28s dev=%llu ino=%llu size=%lld\n", what,
           (unsigned long long) st.st_dev, (unsigned long long) st.st_ino, (long long) st.st_size);
}

int main(void) {
    const char *path = "/tmp/fileid-probe.txt";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { perror("open"); return 1; }
    if (write(fd, "hello\n", 6) != 6) perror("write");
    struct stat a, b, c;
    if (stat(path, &a)) { perror("stat path"); return 1; }
    if (fstat(fd, &b)) { perror("fstat fd"); return 1; }
    printf("parent:\n");
    ids("stat(path)", path);
    { printf("  %-28s dev=%llu ino=%llu size=%lld\n", "fstat(fd)",
             (unsigned long long) b.st_dev, (unsigned long long) b.st_ino, (long long) b.st_size); }

    pid_t pid = fork();
    if (pid == 0) {
        struct stat d;
        if (stat(path, &d)) { perror("child stat"); _exit(2); }
        printf("child:\n");
        printf("  %-28s dev=%llu ino=%llu size=%lld\n", "stat(path)",
               (unsigned long long) d.st_dev, (unsigned long long) d.st_ino, (long long) d.st_size);
        int same_parent = (d.st_dev == a.st_dev && d.st_ino == a.st_ino);
        printf("  same as the parent's stat: %s\n", same_parent ? "yes" : "NO");
        _exit(same_parent ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);

    int bad = 0;
    if (b.st_dev != a.st_dev || b.st_ino != a.st_ino) { printf("stat(path) and fstat(fd) disagree\n"); bad++; }
    if (WEXITSTATUS(status)) { printf("two processes disagree about the same file\n"); bad++; }
    /* and a second stat of the same path must be stable too */
    if (stat(path, &c) || c.st_dev != a.st_dev || c.st_ino != a.st_ino) { printf("stat is not stable\n"); bad++; }
    printf("%s\n", bad ? "FAIL: file identity is inconsistent" : "ok");
    close(fd);
    unlink(path);
    return bad ? 1 : 0;
}
