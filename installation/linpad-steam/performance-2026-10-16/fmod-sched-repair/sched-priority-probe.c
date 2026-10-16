#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
static int checks, failures;
static void check(int ok,const char *name,int actual,int expected) {
    checks++; failures+=!ok;
    printf("SCHED_CHECK %s %s actual=%d expected=%d\n",ok?"PASS":"FAIL",name,actual,expected);
}
static atomic_int worker_policy;
/* musl deliberately stubs sched_getscheduler(); use the Linux ABI directly. */
static void *worker(void *arg) { (void)arg; atomic_store(&worker_policy,(int)syscall(SYS_sched_getscheduler,0)); return NULL; }
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0); alarm(30);
    struct { int policy,min,max; } cases[]={{0,0,0},{1,1,99},{2,1,99},{3,0,0},{5,0,0},{6,0,0},{7,0,0}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        char label[64];errno=0;int a=sched_get_priority_min(cases[i].policy),e=errno;
        snprintf(label,sizeof(label),"policy%d_min",cases[i].policy);check(a==cases[i].min,label,a,cases[i].min);
        printf("SCHED_QUERY policy=%d min_errno=%d\n",cases[i].policy,e);
        errno=0;a=sched_get_priority_max(cases[i].policy);e=errno;
        snprintf(label,sizeof(label),"policy%d_max",cases[i].policy);check(a==cases[i].max,label,a,cases[i].max);
        printf("SCHED_QUERY policy=%d max_errno=%d\n",cases[i].policy,e);
    }
    int invalid[]={-1,4,8,INT_MAX,0x40000000};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        char label[64];errno=0;int a=sched_get_priority_min(invalid[i]),e=errno;
        snprintf(label,sizeof(label),"invalid%d_min",invalid[i]);check(a==-1&&e==EINVAL,label,e,EINVAL);
        errno=0;a=sched_get_priority_max(invalid[i]);e=errno;
        snprintf(label,sizeof(label),"invalid%d_max",invalid[i]);check(a==-1&&e==EINVAL,label,e,EINVAL);
    }
    /* FMOD's exact positive-priority table: SCHED_FIFO at90/94/99. These
     * attributes retain PTHREAD_INHERIT_SCHED; no realtime grant is requested. */
    int priorities[]={90,94,99};
    for(unsigned i=0;i<3;i++) {
        pthread_attr_t attr;int rc=pthread_attr_init(&attr);check(rc==0,"attr_init",rc,0);
        if(rc)continue;
        int inherit=-1;rc=pthread_attr_getinheritsched(&attr,&inherit);
        check(rc==0&&inherit==PTHREAD_INHERIT_SCHED,"inherited_schedule",inherit,PTHREAD_INHERIT_SCHED);
        rc=pthread_attr_setstacksize(&attr,128*1024);check(rc==0,"stack128k",rc,0);
        rc=pthread_attr_setschedpolicy(&attr,SCHED_FIFO);check(rc==0,"attr_fifo",rc,0);
        struct sched_param param={.sched_priority=priorities[i]};
        rc=pthread_attr_setschedparam(&attr,&param);
        printf("FMOD_ATTR priority=%d result=%d text=%s\n",priorities[i],rc,strerror(rc));
        check(rc==0,"fmod_schedparam",rc,0);
        if(rc==0) {
            atomic_store(&worker_policy,-99);
            pthread_t t;rc=pthread_create(&t,&attr,worker,NULL);check(rc==0,"inherited_thread_create",rc,0);
            if(rc==0) { rc=pthread_join(t,NULL);int policy=atomic_load(&worker_policy);check(rc==0&&policy==SCHED_OTHER,"worker_remains_SCHED_OTHER",policy,SCHED_OTHER); }
        }
        pthread_attr_destroy(&attr);
    }
    printf("SCHED_RESULT checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}
