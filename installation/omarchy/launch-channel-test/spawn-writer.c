/* Test harness: the write_launch_script() from ishwl's spawn_command() (extracted verbatim)
 * plus the same bounded FIFO retry, with the surrounding compositor removed. Run inside a
 * container against the supervisor loop omarchy-pad-init uses (mock-init.sh). */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The guest binary is glibc, whose <unistd.h> declares environ; musl needs it spelled out. */
extern char **environ;

#include "writer.inc"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s command\n", argv[0]);
        return 2;
    }
    mkdir("/run/omarchy-pad", 0755);
    char path[128];
    const char *script = write_launch_script(path, sizeof(path), argv[1]);
    if (!script) {
        fprintf(stderr, "could not write launch script: %s\n", strerror(errno));
        return 1;
    }
    fprintf(stderr, "script %s\n", script);
    int lf = -1;
    int waited = 0;
    int budget = 0;
    for (;;) {
        lf = open("/run/omarchy-pad/launch", O_WRONLY | O_NONBLOCK);
        if (lf >= 0) break;
        if (errno == EINTR) {
            if (waited >= 1000) break;
        } else if (errno == ENXIO) {
            budget = 5000;  /* the supervisor exists and will reopen shortly */
        } else if (errno == ENOENT) {
            if (budget < 500) budget = 500;  /* no channel yet: give it a moment */
        } else {
            break;
        }
        if (waited >= budget) break;
        usleep(50 * 1000);
        waited += 50;
    }
    if (lf >= 0 && dprintf(lf, "%s\n", script) > 0) {
        close(lf);
        fprintf(stderr, "request delivered after %dms\n", waited);
        return 0;
    }
    if (lf >= 0) {
        fprintf(stderr, "launch request write failed: %s\n", strerror(errno));
        close(lf);
    } else {
        fprintf(stderr, "launch channel open failed after %dms: %s\n", waited, strerror(errno));
    }
    /* Not delivered: the compositor would fall back to a direct exec, so the script written
     * for the supervisor must not stay behind in /run. */
    unlink(script);
    return 1;
}
