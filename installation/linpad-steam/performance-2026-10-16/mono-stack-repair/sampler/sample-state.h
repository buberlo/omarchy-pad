#ifndef LINPAD_SAMPLE_STATE_H
#define LINPAD_SAMPLE_STATE_H
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define SAMPLE_SIGNAL 23
#define SAMPLE_MAGIC UINT64_C(0x4c5053414d503031)
struct sample_state { uint64_t magic, start_ticks; uint32_t pid, signal, active, reserved; };
static inline int sample_path(char *out, size_t n, long pid) {
    return snprintf(out,n,"/tmp/linpad-x86-sample-%ld.state",pid);
}
static inline uint64_t sample_start(long pid) {
    char path[80], line[4096];
    snprintf(path,sizeof(path),"/proc/%ld/stat",pid);
    FILE *f=fopen(path,"r"); if(!f)return 0;
    char *ok=fgets(line,sizeof(line),f); fclose(f); if(!ok)return 0;
    char *p=strrchr(line,')'); if(!p)return 0; p++;
    for(int field=3;field<=22;field++) {
        while(*p==' ')p++;
        if(field==22)return strtoull(p,NULL,10);
        while(*p && *p!=' ')p++;
    }
    return 0;
}
#endif
