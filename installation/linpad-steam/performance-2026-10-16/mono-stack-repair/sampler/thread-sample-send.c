#define _GNU_SOURCE
#include "sample-state.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>

static int token(long pid){
    char path[96];sample_path(path,sizeof(path),pid);struct sample_state s;struct stat st;
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;
    /* realfs exposes the host file UID while the guest is uid0; use exclusive
     * file permissions plus the live PID/start identity, not UID equality. */
    int ok=fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&!(st.st_mode&077)&&st.st_size==sizeof(s)&&read(fd,&s,sizeof(s))==sizeof(s);close(fd);
    if(!ok||s.magic!=SAMPLE_MAGIC||s.pid!=(uint32_t)pid||s.signal!=SAMPLE_SIGNAL||s.active!=1)return -1;
    uint64_t now=sample_start(pid);
    /* LinPad derives boot-relative starttime on read; one-tick drift was observed. */
    if(!now||now+2<s.start_ticks||s.start_ticks+2<now)return -1;
    return 0;
}
static int send_one(long pid,long tid){
    if(token(pid)){fprintf(stderr,"SAMPLE_REFUSED pid=%ld token absent/inactive/identity mismatch\n",pid);return 2;}
    long rc=syscall(SYS_tgkill,pid,tid,SAMPLE_SIGNAL);int saved=errno;
    printf("SAMPLE_SEND pid=%ld tid=%ld signal=%d result=%ld errno=%d\n",pid,tid,SAMPLE_SIGNAL,rc,rc<0?saved:0);
    return rc<0?3:0;
}
int main(int argc,char **argv){
    if(argc!=3){fprintf(stderr,"usage: thread-sample-send PID TID|all\n");return 64;}
    char *end;long pid=strtol(argv[1],&end,10);if(*end||pid<1)return 64;
    if(strcmp(argv[2],"all")){long tid=strtol(argv[2],&end,10);return *end||tid<1?64:send_one(pid,tid);}
    if(token(pid)){fprintf(stderr,"SAMPLE_REFUSED initial token\n");return 2;}
    char path[80];snprintf(path,sizeof(path),"/proc/%ld/task",pid);DIR *d=opendir(path);if(!d){perror("opendir task");return 3;}
    struct dirent *entry;int result=0,count=0;
    while((entry=readdir(d))&&count<128){long tid=strtol(entry->d_name,&end,10);if(*end||tid<1)continue;int rc=send_one(pid,tid);count++;if(rc){result=rc;break;}usleep(100000);}
    closedir(d);printf("SAMPLE_SEND_COMPLETE count=%d result=%d\n",count,result);return result;
}
