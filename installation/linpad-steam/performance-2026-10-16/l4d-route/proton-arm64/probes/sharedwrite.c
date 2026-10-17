#define _GNU_SOURCE
/* The direction that was never tested: process B maps a file read-only MAP_SHARED, then process A
 * (which also has it mapped) writes THROUGH ITS MAPPING. Wine's server writes the shared session
 * through its own mapping; if that write does not reach pages another process already faulted in,
 * every client keeps the stale copy -- exactly what the device shows (wineserver and late clients
 * see id 0x22, an early client sees 0x12, another sees zeros in the same file at the same offset). */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define N (64 * 1024)
#define PATH "/tmp/shared-write-probe"

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
        /* B: map read-only, fault the page in, then wait while A writes through its mapping */
        int cfd = open(PATH, O_RDONLY);
        char *cm = mmap(NULL, N, PROT_READ, MAP_SHARED, cfd, 0);
        if (cm == MAP_FAILED) { perror("child mmap"); exit(2); }
        volatile unsigned long first = *(volatile unsigned long *) cm;   /* fault it in */
        char go;
        if (read(to_child[0], &go, 1) != 1) exit(3);
        /* give A's write time to land, then look again */
        usleep(300000);
        unsigned long seen = *(volatile unsigned long *) cm;
        printf("  child (mapped before the write, page already faulted): 0x%016lx -> 0x%016lx  %s\n",
               first, seen, seen == 0x5555555555555555UL ? "sees the write (shared)" : "STALE (private copy)");
        if (write(to_parent[1], &seen, sizeof seen) != (ssize_t) sizeof seen) exit(4);
        exit(seen == 0x5555555555555555UL ? 0 : 1);
    }

    close(to_child[0]); close(to_parent[1]);
    char *pm = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pm == MAP_FAILED) { perror("parent mmap"); return 1; }
    usleep(200000);                       /* let the child map and fault its page */
    *(volatile unsigned long *) pm = 0x5555555555555555UL;   /* A writes  THROUGH THE MAPPING */
    msync(pm, 4096, MS_SYNC);
    char go = 'g';
    if (write(to_child[1], &go, 1) != 1) perror("signal");
    int status = 0;
    waitpid(pid, &status, 0);
    /* and what does the file itself say now? */
    unsigned long in_file = 0;
    if (pread(fd, &in_file, sizeof in_file, 0) != (ssize_t) sizeof in_file) perror("pread");
    printf("  file content after A's mapping write:                    0x%016lx  %s\n",
           in_file, in_file == 0x5555555555555555UL ? "reached the file" : "did NOT reach the file");
    int bad = WEXITSTATUS(status) || in_file != 0x5555555555555555UL;
    printf("%s\n", bad ? "FAIL: writes through a shared mapping are not shared" : "ok");
    close(fd); unlink(PATH);
    return bad ? 1 : 0;
}
