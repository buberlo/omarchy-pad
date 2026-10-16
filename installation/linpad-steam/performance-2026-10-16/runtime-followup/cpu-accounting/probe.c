#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

static double wall(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static double usage(void) {
    struct rusage u;
    if (getrusage(RUSAGE_SELF, &u)) exit(2);
    return u.ru_utime.tv_sec + u.ru_stime.tv_sec +
        (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
}
static double stat_cpu(void) {
    char line[2048];
    FILE *f = fopen("/proc/self/stat", "r");
    if (!f || !fgets(line, sizeof(line), f)) exit(3);
    fclose(f);
    char *tail = strrchr(line, ')');
    if (!tail) exit(4);
    char *save, *token = strtok_r(tail + 2, " ", &save);
    unsigned long long ticks = 0;
    for (int field = 3; token; field++, token = strtok_r(NULL, " ", &save))
        if (field == 14 || field == 15) ticks += strtoull(token, NULL, 10);
    return ticks / 100.0;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    double w = wall(), u = usage(), s = stat_cpu();
    struct timespec delay = {.tv_sec = 2};
    if (nanosleep(&delay, NULL)) return 5;
    double sw = wall() - w, su = usage() - u, ss = stat_cpu() - s;
    printf("CPU_ACCOUNTING sleep wall=%.6f getrusage=%.6f proc_stat=%.2f\n", sw, su, ss);
    w = wall(); u = usage(); s = stat_cpu();
    volatile unsigned long value = 1;
    do { for (unsigned i = 0; i < 10000; i++) value = value * 1664525 + 1013904223; }
    while (wall() - w < 1.0);
    double bw = wall() - w, bu = usage() - u, bs = stat_cpu() - s;
    printf("CPU_ACCOUNTING busy wall=%.6f getrusage=%.6f proc_stat=%.2f value=%lu\n", bw, bu, bs, value);
    int ok = sw > 1.9 && su < 0.3 && ss < 0.3 && bu > 0.5 && bs > 0.5;
    printf("CPU_ACCOUNTING %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
