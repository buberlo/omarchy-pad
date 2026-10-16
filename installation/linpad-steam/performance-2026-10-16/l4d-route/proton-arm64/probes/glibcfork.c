/* Wine is a glibc program that threads heavily and forks while threads exist.
 * musl's allocator keeps everything on mmap and survived this; glibc uses brk and per-thread
 * arenas, which is a different code path through the kernel. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile int stop;

static void *churn(void *arg) {
    long id = (long) arg;
    for (int i = 0; i < 3000 && !stop; i++) {
        size_t n = 128 + (size_t) ((i * 7 + id) % 8192);
        unsigned char *p = malloc(n);
        if (!p) { printf("THREAD%d malloc failed\n", (int) id); stop = 1; break; }
        memset(p, (int) id, n);
        for (size_t k = 0; k < n; k++)
            if (p[k] != (unsigned char) id) { printf("THREAD%d CORRUPT n=%zu k=%zu\n", (int) id, n, k); stop = 1; break; }
        free(p);
    }
    return NULL;
}

int main(void) {
    printf("glibc fork+threads probe: pid=%d\n", (int) getpid());
    fflush(stdout);

    /* 1. fork from a threaded process (wine does this constantly) */
    enum { N = 6 };
    pthread_t t[N];
    for (long i = 0; i < N; i++)
        pthread_create(&t[i], NULL, churn, (void *) (i + 1));

    for (int round = 0; round < 12 && !stop; round++) {
        char *buf = malloc(64 * 1024);
        memset(buf, 0xAB, 64 * 1024);
        pid_t pid = fork();
        if (pid == 0) {
            /* child: touch the inherited heap, then leave */
            for (int i = 0; i < 64 * 1024; i++) buf[i] = (char) (i + round);
            _exit(0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        size_t bad = 0;
        for (int i = 0; i < 64 * 1024; i++)
            if ((unsigned char) buf[i] != 0xAB) bad++;
        if (bad) { printf("PARENT HEAP CHANGED after fork round %d: %zu bytes\n", round, bad); stop = 1; }
        free(buf);
    }
    printf("fork-while-threaded: %s\n", stop ? "FAILED" : "ok");

    for (int i = 0; i < N; i++) pthread_join(t[i], NULL);
    printf("thread churn: %s\n", stop ? "FAILED" : "ok");
    printf("summary: %s\n", stop ? "CORRUPTION" : "glibc threads+fork ok");
    return stop ? 1 : 0;
}
