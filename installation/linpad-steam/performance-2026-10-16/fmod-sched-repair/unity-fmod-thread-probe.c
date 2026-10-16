#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
/* Diagnostic only, exact UnityPlayer SHA256:
 * 12ce7af93c41b13b3e7d9b75b591336cc56c9ce113714143fd897cc1dcf00e33.
 * Calls only its original Linux thread helper, never PlayerMain/FMOD System init.
 * No audio stream, graphics, device configuration or machine-code edits. */
static atomic_int done, policy;
static void *worker(void *unused) {
    (void)unused;
    atomic_store(&policy,(int)syscall(SYS_sched_getscheduler,0));
    atomic_store(&done,1);
    return NULL;
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);alarm(30);
    const char *path=getenv("UNITY_PLAYER_PATH");if(!path)path="/opt/UnityPlayer.so";
    void *h=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!h){printf("UNITY_LOAD_FAIL %s\n",dlerror());return 2;}
    void *entry=dlsym(h,"_Z10PlayerMainiPPc");Dl_info info;
    if(!entry||!dladdr(entry,&info))return 3;
    unsigned char *base=info.dli_fbase;
    const unsigned char guard[]={0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x48,0x44,0x89};
    if(memcmp(base+0x84ecb0,guard,sizeof(guard))){puts("UNITY_FMOD_THREAD_GUARD_FAIL");return 65;}
    typedef int (*create_fn)(const char *,void *(*)(void *),void *,int,unsigned,unsigned,pthread_t *);
    create_fn create=(void *)(base+0x84ecb0);int failures=0;
    puts("UNITY_FMOD_THREAD_GUARD_PASS offset=0x84ecb0 expected_sha256=12ce7af93c41b13b3e7d9b75b591336cc56c9ce113714143fd897cc1dcf00e33");
    for(int priority=0;priority<=3;priority++) {
        atomic_store(&done,0);atomic_store(&policy,-99);pthread_t thread=0;
        int result=create("diagnostic",worker,NULL,priority,0,128*1024,&thread);
        if(!result)for(int i=0;i<2000&&!atomic_load(&done);i++)usleep(1000);
        int completed=atomic_load(&done),p=atomic_load(&policy);
        printf("UNITY_FMOD_THREAD priority_enum=%d result=%d worker_done=%d actual_policy=%d\n",priority,result,completed,p);
        failures+=(result!=0||completed!=1||p!=0);
    }
    printf("UNITY_FMOD_THREAD_RESULT checks=4 failures=%d\n",failures);
    /* Avoid unrelated Unity global destructors after this isolated probe. */
    fflush(stdout);_exit(failures?1:0);
}
