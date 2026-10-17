#define _GNU_SOURCE
/* Wine's server keeps the shared session in a file and writes it with file I/O (write/pwrite),
 * while every client reads that file through a read-only MAP_SHARED mapping. If file writes do not
 * become visible in another process's existing mapping -- or a mapping's writes do not reach the
 * file -- the client reads stale data, which is exactly "Session object id doesn't match expected
 * id". Deterministic: pipe handshakes, unbuffered child output. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define PAGE 4096
#define N (64 * 1024)
#define PATH "/tmp/coherence2-probe"
#define MARK_A 0x1111111111111111UL
#define MARK_B 0x2222222222222222UL

static void put(int fd, unsigned long v) { if (write(fd, &v, sizeof v) != (ssize_t) sizeof v) _exit(9); }
static unsigned long get(int fd) { unsigned long v = 0; if (read(fd, &v, sizeof v) != (ssize_t) sizeof v) _exit(9); return v; }

int main(void) {
    int fd = open(PATH, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { perror("open"); return 1; }
    if (ftruncate(fd, N)) { perror("ftruncate"); return 1; }
    int to_child[2], to_parent[2];
    if (pipe(to_child) || pipe(to_parent)) { perror("pipe"); return 1; }

    pid_t pid = fork();
    if (pid == 0) {
        setvbuf(stdout, NULL, _IONBF, 0);
        close(to_child[1]); close(to_parent[0]);
        int cfd = open(PATH, O_RDONLY);
        if (cfd < 0) { perror("child open"); exit(2); }
        unsigned long *cm = mmap(NULL, N, PROT_READ, MAP_SHARED, cfd, 0);
        if (cm == MAP_FAILED) { perror("child mmap"); exit(3); }
        int wfd = open(PATH, O_RDWR);
        unsigned long *wm = mmap(NULL, PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, wfd, 0);
        if (wm == MAP_FAILED) { perror("child mmap rw"); exit(4); }

        get(to_child[0]);                       /* the parent has written to the file */
        printf("  child's read-only mapping sees the parent's pwrite(): %s (0x%lx)\n",
               cm[0] == MARK_A ? "yes" : "NO", cm[0]);
        put(to_parent[1], cm[0] == MARK_A ? 1UL : 0UL);

        wm[8] = MARK_B;                          /* write through a mapping */
        msync(wm, PAGE, MS_SYNC);
        put(to_parent[1], 2UL);
        exit(0);
    }

    close(to_child[0]); close(to_parent[1]);
    unsigned long *pm = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pm == MAP_FAILED) { perror("parent mmap"); return 1; }
    pm[0] = 0;
    unsigned long marker = MARK_A;
    if (pwrite(fd, &marker, sizeof marker, 0) != (ssize_t) sizeof marker) { perror("pwrite"); return 1; }
    put(to_child[1], 1UL);
    int bad = 0;
    if (get(to_parent[0]) != 1UL) { printf("  child did NOT see the file write through its mapping\n"); bad++; }
    if (get(to_parent[0]) != 2UL) { printf("  child failed after the check\n"); bad++; }
    int status = 0;
    waitpid(pid, &status, 0);
    unsigned long back = 0;
    if (pread(fd, &back, sizeof back, 8) != (ssize_t) sizeof back) perror("pread");
    printf("  parent's pread() sees the child's mapping write: %s (0x%lx)\n",
           back == MARK_B ? "yes" : "NO", back);
    if (back != MARK_B) bad++;
    printf("%s\n", bad ? "FAIL: file I/O and mappings are not coherent" : "ok");
    close(fd); unlink(PATH);
    return bad ? 1 : 0;
}
