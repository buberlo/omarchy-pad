#define _GNU_SOURCE
/* Every earlier probe created the file at full size first. Wine does not: it maps the session while
 * the file is still small and grows it later, and the processes that mapped it early (left4dead.exe,
 * plugplay.exe) are exactly the ones that read garbage or zeros while late ones (65 explorer.exe
 * instances) read the server's data. Sequence reproduced: full-size mapping over a small file, a page
 * faulted before the growth, then ftruncate + a write through the writer's mapping. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define SMALL (64 * 1024)
#define BIG   (1024 * 1024)
#define PATH "/tmp/grow-map-probe"
#define MARK  0x7777777777777777UL

int main(void) {
    int fd = open(PATH, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { perror("open"); return 1; }
    if (ftruncate(fd, SMALL)) { perror("ftruncate small"); return 1; }
    int to_child[2], to_parent[2];
    if (pipe(to_child) || pipe(to_parent)) { perror("pipe"); return 1; }

    pid_t pid = fork();
    if (pid == 0) {
        setvbuf(stdout, NULL, _IONBF, 0);
        close(to_child[1]); close(to_parent[0]);
        int cfd = open(PATH, O_RDONLY);
        /* map the full size while the file is still small, as wine does */
        char *cm = mmap(NULL, BIG, PROT_READ, MAP_SHARED, cfd, 0);
        if (cm == MAP_FAILED) { perror("child mmap"); exit(2); }
        unsigned long first = *(volatile unsigned long *) cm;      /* fault page 0 only */
        char go;
        if (read(to_child[0], &go, 1) != 1) exit(3);
        usleep(300000);
        unsigned long high = *(volatile unsigned long *) (cm + 0xf000);   /* a page far beyond the old end */
        printf("  child: page0 0x%016lx, page@0xf000 0x%016lx  (want 0x%016lx)  %s\n",
               first, high, MARK, high == MARK ? "sees the later write (shared)" : "STALE -- the grown region never reached this mapping");
        exit(high == MARK ? 0 : 1);
    }

    close(to_child[0]); close(to_parent[1]);
    char *pm = mmap(NULL, BIG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pm == MAP_FAILED) { perror("parent mmap"); return 1; }
    usleep(200000);
    if (ftruncate(fd, BIG)) { perror("ftruncate big"); }          /* grow while both mappings exist */
    *(volatile unsigned long *) (pm + 0xf000) = MARK;              /* write into the grown region */
    char go = 'g';
    if (write(to_child[1], &go, 1) != 1) perror("signal");
    int status = 0;
    waitpid(pid, &status, 0);
    int bad = WEXITSTATUS(status);
    printf("%s\n", bad ? "FAIL: a mapping does not see data written after the file grew" : "ok");
    close(fd); unlink(PATH);
    return bad ? 1 : 0;
}
