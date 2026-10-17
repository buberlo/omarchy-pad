#define _GNU_SOURCE
/* The exact combination wine uses, and the only one never tested: the writer (wineserver) has the
 * file mapped MAP_SHARED and writes THROUGH ITS MAPPING, while the reader (a wine client, mapping a
 * read-only section view) has it mapped MAP_PRIVATE, as the device shows:
 *     pid 692 prot 0x83 guestflags 0x1  hostflags 0x1   MAP_SHARED  (writer)
 *     pid 716 prot 0x3  guestflags 0x12 hostflags 0x2   MAP_PRIVATE (reader)
 * On Linux a MAP_PRIVATE mapping is copy-on-write over the page cache, so the reader still sees the
 * writer's pages. If this filesystem copies eagerly, the reader keeps a snapshot from map time --
 * which is exactly why wine's early clients read stale bytes and late ones read the current ones. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define N 4096
#define PATH "/tmp/mix-map-probe"
#define MARK 0x8888888888888888UL

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
        unsigned long *cm = mmap(NULL, N, PROT_READ, MAP_PRIVATE, cfd, 0);   /* wine's client */
        if (cm == MAP_FAILED) { perror("child mmap"); exit(2); }
        unsigned long before = cm[0];                       /* fault the page in first */
        char go = 'r';
        if (write(to_parent[1], &go, 1) != 1) exit(3);      /* tell the writer we are ready */
        if (read(to_child[0], &go, 1) != 1) exit(4);        /* wait for the write */
        usleep(300000);
        unsigned long after = cm[0];
        printf("  reader (MAP_PRIVATE, page faulted before the write): 0x%016lx -> 0x%016lx  %s\n",
               before, after,
               after == MARK ? "sees the writer's mapping write (copy-on-write over the page cache)"
                             : "STALE SNAPSHOT -- the private mapping was copied at map time");
        exit(after == MARK ? 0 : 1);
    }

    close(to_child[0]); close(to_parent[1]);
    unsigned long *pm = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);   /* wine's server */
    if (pm == MAP_FAILED) { perror("parent mmap"); return 1; }
    char go;
    if (read(to_parent[0], &go, 1) != 1) return 1;          /* the reader has faulted its page */
    pm[0] = MARK;                                           /* write through the mapping, not pwrite */
    if (write(to_child[1], &go, 1) != 1) perror("signal");
    int status = 0;
    waitpid(pid, &status, 0);
    int bad = WEXITSTATUS(status);
    printf("%s\n", bad ? "FAIL: a MAP_PRIVATE reader does not see a MAP_SHARED writer's pages"
                       : "ok");
    close(fd); unlink(PATH);
    return bad ? 1 : 0;
}
