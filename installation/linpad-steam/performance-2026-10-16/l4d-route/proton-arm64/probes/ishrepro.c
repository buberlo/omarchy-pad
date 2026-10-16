/* Does this emulator keep a process's heap intact across fork() and threads?
 * Runs natively (no FEX): it isolates the Linux emulation, not the guest translation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>

static int fork_heap(void) {
    for (int round = 0; round < 40; round++) {
        size_t n = 4096 + (size_t) round * 1024;
        char *p = malloc(n);
        if (!p) { printf("malloc failed at round %d\n", round); return 1; }
        for (size_t i = 0; i < n; i++) p[i] = (char) (i ^ round);
        pid_t pid = fork();
        if (pid == 0) { for (size_t i = 0; i < n; i++) p[i] = (char) (i + 1); _exit(0); }
        int status = 0; waitpid(pid, &status, 0);
        for (size_t i = 0; i < n; i++) {
            if (p[i] != (char) (i ^ round)) {
                printf("FORK_HEAP_CORRUPT round=%d offset=%zu got=%d want=%d\n",
                       round, i, (int) (unsigned char) p[i], (int) (unsigned char) (i ^ round));
                return 1;
            }
        }
        free(p);
    }
    printf("fork_heap ok\n");
    return 0;
}

static void *worker(void *arg) {
    long id = (long) arg;
    for (int round = 0; round < 200; round++) {
        size_t n = 8192;
        char *p = malloc(n);
        if (!p) return (void *) 1;
        memset(p, (int) id, n);
        for (size_t i = 0; i < n; i++) if (p[i] != (char) id) { free(p); return (void *) 1; }
        free(p);
    }
    return NULL;
}

static int thread_heap(void) {
    enum { N = 8 };
    pthread_t t[N];
    for (long i = 0; i < N; i++)
        if (pthread_create(&t[i], NULL, worker, (void *) i) != 0) { printf("pthread_create failed\n"); return 1; }
    int bad = 0;
    for (int i = 0; i < N; i++) { void *r = NULL; pthread_join(t[i], &r); if (r) bad = 1; }
    printf("thread_heap %s\n", bad ? "CORRUPT" : "ok");
    return bad;
}

int main(void) {
    printf("ish-repro: pid=%d\n", (int) getpid());
    int a = fork_heap();
    int b = thread_heap();
    printf("summary: fork_heap=%d thread_heap=%d\n", a, b);
    return a || b;
}
