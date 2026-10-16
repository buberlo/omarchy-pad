#define _GNU_SOURCE
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/resource.h>

/* Read-only glibc main-thread stack inference regression, not a game test. */
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    struct rlimit limit;
    pthread_attr_t attr;
    void *base = NULL;
    size_t bytes = 0;
    int local;
    int lr = getrlimit(RLIMIT_STACK, &limit);
    int ar = pthread_getattr_np(pthread_self(), &attr);
    int sr = ar ? -1 : pthread_attr_getstack(&attr, &base, &bytes);
    if (!ar) pthread_attr_destroy(&attr);
    if (lr || ar || sr) {
        printf("STACK_LIMITS FAIL getrlimit=%d getattr=%d getstack=%d\n", lr, ar, sr);
        return 1;
    }
    uintptr_t start = (uintptr_t)base, here = (uintptr_t)&local;
    printf("STACK_LIMITS local=%p base=%p size=0x%zx soft=0x%llx hard=0x%llx\n",
           (void *)&local, base, bytes, (unsigned long long)limit.rlim_cur,
           (unsigned long long)limit.rlim_max);
    int contains = bytes && here >= start && here - start < bytes;
    int bounded = limit.rlim_cur != RLIM_INFINITY && bytes <= limit.rlim_cur;
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) { perror("maps"); return 2; }
    char line[1024];
    int found = 0, map_bounded = 0;
    while (fgets(line, sizeof(line), maps)) {
        uintptr_t lo, hi;
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR, &lo, &hi) == 2 &&
            lo <= here && here < hi) {
            printf("STACK_LIMITS MAP %s", line);
            found++;
            map_bounded = hi > lo && limit.rlim_cur != RLIM_INFINITY &&
                          hi - lo <= limit.rlim_cur;
        }
    }
    fclose(maps);
    printf("STACK_LIMITS %s contains_local=%d within_soft_limit=%d one_mapping=%d mapping_bounded=%d\n",
           contains && bounded && found == 1 && map_bounded ? "PASS" : "FAIL",
           contains, bounded, found == 1, map_bounded);
    return contains && bounded && found == 1 && map_bounded ? 0 : 1;
}
