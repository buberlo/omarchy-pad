#define _GNU_SOURCE
/* Every earlier probe mapped the file *after* forking, in each process separately. Wine does it the
 * other way round: wineserver maps the session and later processes inherit that mapping across fork.
 * If fork copies a MAP_SHARED file mapping (copy-on-write), each child keeps a private snapshot --
 * which is exactly what the device shows: the server and the clients forked after its writes
 * (65 explorer instances) read its data, while the clients forked before them (left4dead.exe,
 * plugplay.exe) read stale values, all in the same file at the same offset. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define N 4096
#define PATH "/tmp/fork-map-probe"
#define BEFORE 0x1111111111111111UL
#define AFTER  0x2222222222222222UL

int main(void) {
    int fd = open(PATH, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { perror("open"); return 1; }
    if (ftruncate(fd, N)) { perror("ftruncate"); return 1; }
    unsigned long *m = mmap(NULL, N, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { perror("mmap"); return 1; }
    m[0] = BEFORE;                       /* the state the child will inherit */

    int to_child[2], to_parent[2];
    if (pipe(to_child) || pipe(to_parent)) { perror("pipe"); return 1; }

    pid_t pid = fork();                  /* fork AFTER the mapping exists, as wine does */
    if (pid == 0) {
        setvbuf(stdout, NULL, _IONBF, 0);
        close(to_child[1]); close(to_parent[0]);
        unsigned long inherited = m[0];
        char go;
        if (read(to_child[0], &go, 1) != 1) exit(3);
        usleep(300000);
        unsigned long after = m[0];
        int ok = (inherited == BEFORE) && (after == AFTER);
        printf("  child (inherited the mapping across fork): 0x%016lx -> 0x%016lx  %s\n",
               inherited, after,
               ok ? "sees the parent's later write (shared)" : "STALE SNAPSHOT (fork copied the shared mapping)");
        exit(ok ? 0 : 1);
    }
    close(to_child[0]); close(to_parent[1]);
    usleep(200000);
    m[0] = AFTER;                        /* the parent writes through its mapping after the fork */
    char go = 'g';
    if (write(to_child[1], &go, 1) != 1) perror("signal");
    int status = 0;
    waitpid(pid, &status, 0);
    unsigned long in_file = 0;
    if (pread(fd, &in_file, sizeof in_file, 0) != (ssize_t) sizeof in_file) perror("pread");
    printf("  file content: 0x%016lx  %s\n", in_file, in_file == AFTER ? "(the parent's write)" : "(unexpected)");
    int bad = WEXITSTATUS(status);
    printf("%s\n", bad ? "FAIL: fork breaks MAP_SHARED file mappings" : "ok");
    close(fd); unlink(PATH);
    return bad ? 1 : 0;
}
