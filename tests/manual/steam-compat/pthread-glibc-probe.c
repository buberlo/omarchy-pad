#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static void *worker(void *arg) {return arg;}
int main(void) {
    setbuf(stdout,NULL);
    for(int round=0;round<10;round++) {
        pthread_t threads[16];
        for(int i=0;i<16;i++) {
            pthread_attr_t attr;pthread_attr_init(&attr);
            size_t sizes[]={0,65536,1024*1024,8*1024*1024};
            if(sizes[i%4])pthread_attr_setstacksize(&attr,sizes[i%4]);
            size_t size;pthread_attr_getstacksize(&attr,&size);
            printf("create round=%d thread=%d stack=%zu\n",round,i,size);
            int e=pthread_create(&threads[i],&attr,worker,(void*)(uintptr_t)(i+1));
            pthread_attr_destroy(&attr);if(e){printf("ERROR %d\n",e);return 1;}
        }
        for(int i=0;i<16;i++){void *p;if(pthread_join(threads[i],&p)||p!=(void*)(uintptr_t)(i+1))return 2;}
    }
    puts("GLIBC_THREAD_STACKS_OK");return 0;
}
