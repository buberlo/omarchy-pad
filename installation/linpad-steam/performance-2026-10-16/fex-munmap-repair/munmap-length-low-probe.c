#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static int failures;
static void check_unmap(const char *kind, int fd, size_t length, off_t offset) {
    void *p=mmap(NULL,length,PROT_READ|PROT_WRITE,
                 MAP_PRIVATE | MAP_32BIT | (fd<0 ? MAP_ANONYMOUS : 0),fd,offset);
    if(p==MAP_FAILED) { perror("mmap"); failures++; return; }
    if ((uintptr_t)p >= UINT64_C(0x100000000)) { fprintf(stderr,"mapping unexpectedly above 4GiB\n"); failures++; munmap(p,length); return; }
    errno=0;
    int result=munmap(p,length),error=errno;
    printf("%s len=%zu offset=%lld addr=%p munmap=%d errno=%d %s\n",
           kind,length,(long long)offset,p,result,error,result==0?"PASS":"FAIL");
    if(result) { failures++; munmap(p,(length+4095)&~(size_t)4095); }
}
int main(void) {
    alarm(20);
    printf("page_size=%ld\n",sysconf(_SC_PAGESIZE));
    size_t lengths[]={1,129,4095,4096,4097,8191,16385};
    char path[]="/tmp/munmap-length-probe-XXXXXX";
    int fd=mkstemp(path);
    if(fd<0 || ftruncate(fd,65536)) return 2;
    unlink(path);
    for(size_t i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++) {
        check_unmap("anon",-1,lengths[i],0);
        check_unmap("file",fd,lengths[i],0);
    }
    check_unmap("file-offset",fd,5001,4096);
    close(fd);
    unsigned char *p=mmap(NULL,12288,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_32BIT|MAP_ANONYMOUS,-1,0);
    if(p==MAP_FAILED) return 2;
    errno=0;
    int r=munmap(p+1,4096),e=errno;
    printf("unaligned_address result=%d errno=%d %s\n",r,e,r==-1&&e==EINVAL?"PASS":"FAIL");
    failures+=!(r==-1&&e==EINVAL);
    errno=0;r=munmap(p,0);e=errno;
    printf("zero_length result=%d errno=%d %s\n",r,e,r==-1&&e==EINVAL?"PASS":"FAIL");
    failures+=!(r==-1&&e==EINVAL);
    errno=0;r=munmap(p+4096,1);e=errno;
    p[0]=17;p[8192]=31;
    printf("partial_4k_unmap result=%d errno=%d neighbors=%u,%u %s\n",
           r,e,p[0],p[8192],r==0?"PASS":"FAIL");
    failures+=r!=0;
    munmap(p,12288);
    printf("MUNMAP_PROBE failures=%d\n",failures);
    return failures?1:0;
}
