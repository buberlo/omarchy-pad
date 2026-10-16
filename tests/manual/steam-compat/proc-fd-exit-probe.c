#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
int main(void) {
    setbuf(stdout, NULL);
    unsigned scans = 0;
    for (unsigned round = 0; round < 300; round++) {
        int gate[2];
        if (pipe(gate)) return 90;
        pid_t pid = fork();
        if (pid < 0) return 91;
        if (!pid) {
            close(gate[1]);
            char byte;
            if (read(gate[0], &byte, 1) != 1) _exit(92);
            _exit(0);
        }
        close(gate[0]);
        char path[96];
        snprintf(path, sizeof(path), "/proc/%d/fd", pid);
        DIR *dir = opendir(path);
        if (write(gate[1], "x", 1) != 1) return 93;
        close(gate[1]);
        for (unsigned n = 0; n < 12; n++) {
            if (dir) {
                unsigned entries = 0;
                while (readdir(dir)) {
                    if (++entries > 128) {
                        fprintf(stderr, "FAIL: directory did not terminate\n");
                        return 1;
                    }
                }
                closedir(dir);
                scans++;
            }
            dir = n < 11 ? opendir(path) : NULL;
        }
        int status;
        if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status)) return 94;
    }
    printf("PROC_FD_EXIT_SCAN_OK rounds=300 scans=%u\n", scans);
    return 0;
}
