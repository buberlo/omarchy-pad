#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <emmintrin.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int count;
static volatile sig_atomic_t delivered;
static volatile double vector_input[4] = {1.5, 4.0, 2.5, 2.0};
static void on_signal(int n) { delivered = n; }
static void *worker(void *unused) {
    for (int i = 0; i < 1000; i++) {
        pthread_mutex_lock(&mutex); count++; pthread_mutex_unlock(&mutex);
    }
    return unused;
}

int main(void) {
    pthread_t threads[2];
    for (int i = 0; i < 2; i++) if (pthread_create(&threads[i], 0, worker, 0)) return 10;
    for (int i = 0; i < 2; i++) if (pthread_join(threads[i], 0)) return 11;
    if (count != 2000) return 12;
    puts("FEX threads/mutex: PASS");
    double values[2];
    _mm_storeu_pd(values, _mm_add_pd(_mm_set_pd(vector_input[1], vector_input[0]),
                                  _mm_set_pd(vector_input[3], vector_input[2])));
    if (values[0] != 4.0 || values[1] != 6.0) return 20;
    puts("FEX SSE2 arithmetic: PASS");
    unsigned char *code = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) return 30;
    memcpy(code, "\xb8\x2a\x00\x00\x00\xc3", 6);
    if (mprotect(code, 4096, PROT_READ | PROT_EXEC)) return 31;
    if (((int (*)(void))code)() != 42) return 32;
    munmap(code, 4096);
    puts("FEX generated x86 code: PASS");
    signal(SIGUSR1, on_signal);
    if (raise(SIGUSR1) || delivered != SIGUSR1) return 40;
    puts("FEX signal delivery: PASS");
    puts("FEX_X86_RUNTIME_OK");
    return 0;
}
