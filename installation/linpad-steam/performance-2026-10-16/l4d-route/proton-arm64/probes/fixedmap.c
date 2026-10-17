#define _GNU_SOURCE
/* Wine maps every view at a fixed address (map_view with base=0x710000, MAP_FIXED in the unix
 * layer). None of the earlier probes used MAP_FIXED. If a MAP_FIXED mapping of a file becomes a
 * private copy in this filesystem, wine's clients never see the server's writes -- which is what the
 * device shows: the server and late clients read 0x22 at +0xfff70, an early client 0x12, another
 * zeros, all in the same file at the same offset. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define N (64 * 1024)
#define PATH "/tmp/fixed-map-probe"
#define BASE_A 0x0000000071000000UL   /* deliberately far apart, like wine's clients */
#define BASE_B 0x000000007e000000UL
#define MARK 0x6666666666666666UL

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
        /* B: map at ITS OWN fixed address, read-only, and fault the page in */
        char *cm = mmap((void *) BASE_B, N, PROT_READ, MAP_SHARED | MAP_FIXED, cfd, 0);
        if (cm == MAP_FAILED) { perror("child mmap"); exit(2); }
        unsigned long first = *(volatile unsigned long *) cm;
        char go;
        if (read(to_child[0], &go, 1) != 1) exit(3);
        usleep(300000);
        unsigned long seen = *(volatile unsigned long *) cm;
        printf("  child (MAP_FIXED at %#lx, page faulted before the write): 0x%016lx -> 0x%016lx  %s\n",
               (unsigned long) BASE_B, first, seen,
               seen == MARK ? "sees the write (shared)" : "STALE (private copy)");
        exit(seen == MARK ? 0 : 1);
    }

    close(to_child[0]); close(to_parent[1]);
    /* A: map at ITS OWN fixed address and write through it */
    char *pm = mmap((void *) BASE_A, N, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (pm == MAP_FAILED) { perror("parent mmap"); return 1; }
    usleep(200000);
    *(volatile unsigned long *) pm = MARK;
    msync(pm, 4096, MS_SYNC);
    char go = 'g';
    if (write(to_child[1], &go, 1) != 1) perror("signal");
    int status = 0;
    waitpid(pid, &status, 0);
    unsigned long in_file = 0;
    if (pread(fd, &in_file, sizeof in_file, 0) != (ssize_t) sizeof in_file) perror("pread");
    printf("  file after A's mapping write: 0x%016lx  %s\n", in_file,
           in_file == MARK ? "reached the file" : "did NOT reach the file");
    int bad = WEXITSTATUS(status) || in_file != MARK;
    printf("%s\n", bad ? "FAIL: MAP_FIXED shared mappings are not shared" : "ok");
    close(fd); unlink(PATH);
    return bad ? 1 : 0;
}
