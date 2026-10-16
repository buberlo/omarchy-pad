#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
static atomic_int ready, release_workers;
static int tids[8];
static void *worker(void *arg) {
    unsigned n = (unsigned)(size_t)arg;
    tids[n] = syscall(SYS_gettid);
    atomic_fetch_add(&ready, 1);
    while (!atomic_load(&release_workers)) usleep(1000);
    return NULL;
}
int main(void) {
    pthread_t threads[8];
    for (unsigned n=0;n<8;n++) if(pthread_create(&threads[n],NULL,worker,(void *)(size_t)n)) return 90;
    while (atomic_load(&ready)!=8) usleep(1000);
    int failed=0, leader_seen=0;
    DIR *dir=opendir("/proc"); if(!dir)return 91;
    struct dirent *de;
    while ((de=readdir(dir))) {
        int pid=atoi(de->d_name);
        if(pid==getpid())leader_seen=1;
        for(unsigned n=0;n<8;n++) if(pid==tids[n])failed=1;
    }
    closedir(dir);
    for(unsigned n=0;n<8;n++) {
        char path[96]; struct stat st;
        snprintf(path,sizeof(path),"/proc/%d/status",tids[n]);
        if(stat(path,&st))failed=1;
        snprintf(path,sizeof(path),"/proc/self/task/%d",tids[n]);
        if(stat(path,&st))failed=1;
    }
    atomic_store(&release_workers,1);
    for(unsigned n=0;n<8;n++)pthread_join(threads[n],NULL);
    printf("PROC_THREAD_LIST_%s leader=%d hidden_workers=8 direct_lookup=8 task_lookup=8\n",!failed&&leader_seen?"OK":"FAIL",leader_seen);
    return failed||!leader_seen;
}
