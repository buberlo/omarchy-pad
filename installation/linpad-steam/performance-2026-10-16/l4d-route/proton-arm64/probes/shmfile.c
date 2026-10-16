/* Wine's server and each client are SEPARATE processes that each open() and mmap() the same
 * shared file. That only works if the file's pages are really shared between mappings. A fork()
 * test does not prove it -- the child inherits the mapping. This probe makes the child open and
 * map the file independently. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define N 8192

static int child_side(const char *path, int rfd, int wfd) {
    /* Wait for the parent's signal, then map the file independently and check what it sees. */
    char go;
    if (read(rfd, &go, 1) != 1) _exit(2);
    int fd = open(path, O_RDWR);
    if (fd < 0) { printf("child: open failed: %s\n", strerror(errno)); _exit(3); }
    char *p = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { printf("child: mmap failed: %s\n", strerror(errno)); _exit(4); }
    int seen = 0;
    for (int i = 0; i < N; i++)
        if (p[i] == (char) (i & 0xff)) seen++;
    printf("child: bytes from the parent's write that it can see: %d of %d\n", seen, N);
    /* write its own pattern into the second half */
    for (int i = N / 2; i < N; i++) p[i] = (char) (0xff - (i & 0xff));
    char done = 'd';
    if (write(wfd, &done, 1) != 1) _exit(5);
    _exit(seen == N ? 0 : 1);
}

int main(void) {
    const char *paths[] = {"/dev/shm/linpad-shmfile-probe", "/tmp/linpad-shmfile-probe"};
    int bad = 0;
    for (int t = 0; t < 2; t++) {
        const char *path = paths[t];
        unlink(path);
        int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) { printf("%-34s open failed: %s\n", path, strerror(errno)); bad++; continue; }
        if (ftruncate(fd, N)) perror("ftruncate");
        char *p = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) { printf("%-34s mmap failed: %s\n", path, strerror(errno)); close(fd); bad++; continue; }
        for (int i = 0; i < N; i++) p[i] = (char) (i & 0xff);

        int to_child[2], from_child[2];
        if (pipe(to_child) || pipe(from_child)) { perror("pipe"); return 1; }
        pid_t pid = fork();
        if (pid == 0) {
            close(to_child[1]); close(from_child[0]);
            child_side(path, to_child[0], from_child[1]);
        }
        close(to_child[0]); close(from_child[1]);
        char go = 'g';
        if (write(to_child[1], &go, 1) != 1) { printf("signal failed\n"); return 1; }
        char done;
        if (read(from_child[0], &done, 1) != 1) { printf("child died\n"); }
        int status = 0;
        waitpid(pid, &status, 0);

        int changed = 0;
        for (int i = N / 2; i < N; i++)
            if ((unsigned char) p[i] == (unsigned char) (0xff - (i & 0xff))) changed++;
        printf("%-34s parent sees %d of %d bytes the child wrote (child exit=%d)\n",
               path, changed, N / 2, WEXITSTATUS(status));
        if (changed != N / 2 || WEXITSTATUS(status) != 0) bad++;
        munmap(p, N);
        close(fd);
        unlink(path);
    }
    printf("summary: %s\n", bad ? "SEPARATE MAPPINGS ARE NOT SHARED" : "file-backed shared memory ok");
    return bad ? 1 : 0;
}
