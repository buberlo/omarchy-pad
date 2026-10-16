#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
/* Opaque API declarations checked against official PulseAudio v17.0 headers. */
typedef struct pa_context pa_context;
typedef struct pa_mainloop pa_mainloop;
typedef struct pa_mainloop_api pa_mainloop_api;
typedef struct pa_operation pa_operation;
typedef struct pa_sink_info pa_sink_info;
typedef struct pa_spawn_api pa_spawn_api;
typedef void (*sink_cb_t)(pa_context *, const pa_sink_info *, int, void *);
static pa_mainloop *(*p_mainloop_new)(void);
static void (*p_mainloop_free)(pa_mainloop *);
static pa_mainloop_api *(*p_mainloop_get_api)(pa_mainloop *);
static int (*p_mainloop_iterate)(pa_mainloop *,int,int *);
static pa_context *(*p_context_new)(pa_mainloop_api *,const char *);
static void (*p_context_unref)(pa_context *);
static int (*p_context_connect)(pa_context *,const char *,int,const pa_spawn_api *);
static void (*p_context_disconnect)(pa_context *);
static int (*p_context_get_state)(const pa_context *);
static int (*p_context_errno)(const pa_context *);
static pa_operation *(*p_context_get_sink_info_by_name)(pa_context *,const char *,sink_cb_t,void *);
static void (*p_operation_unref)(pa_operation *);
static const char *(*p_strerror)(int);
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
struct state { int done, found, failed; };
static void sink_cb(pa_context *c,const pa_sink_info *i,int eol,void *ud) {
    (void)c; struct state *s=ud;
    if(eol) { s->done=1; s->failed=eol<0; } else if(i) s->found++;
}
static int safe_setup(void) {
    /* Only a known synthetic 256-byte zero token may be read. If any later
     * library fallback occurs, HOME/XDG_CONFIG_HOME point to the same isolated
     * diagnostic directory, never the user's authentication-cookie location. */
    const char *base="/tmp/linpad-pulse-probe";
    const char *token="/tmp/linpad-pulse-probe/zero-token";
    const char *config="/tmp/linpad-pulse-probe/client.conf";
    struct stat st; unsigned char bytes[257];
    if(lstat(base,&st)||!S_ISDIR(st.st_mode)) return -1;
    if(lstat(token,&st)||!S_ISREG(st.st_mode)||st.st_size!=256) return -1;
    FILE *f=fopen(token,"rb"); if(!f) return -1;
    size_t n=fread(bytes,1,sizeof(bytes),f); fclose(f);
    if(n!=256) return -1;
    for(size_t i=0;i<n;i++) if(bytes[i]) return -1;
    if(lstat(config,&st)||!S_ISREG(st.st_mode)) return -1;
    unsetenv("DISPLAY"); unsetenv("PULSE_SERVER"); unsetenv("PULSE_RUNTIME_PATH");
    unsetenv("PULSE_SINK"); unsetenv("PULSE_SOURCE"); unsetenv("PULSE_PROP");
    unsetenv("PULSE_BINARY"); unsetenv("PULSE_CONFIG"); unsetenv("PULSE_SCRIPT");
    if(setenv("HOME",base,1)||setenv("XDG_CONFIG_HOME",base,1)||
       setenv("PULSE_COOKIE",token,1)||setenv("PULSE_CLIENTCONFIG",config,1)) return -1;
    return 0;
}
#define LOAD(name) do { *(void **)(&p_##name)=dlsym(lib,"pa_" #name); if(!p_##name) { printf("MISSING_SYMBOL=pa_%s\n",#name); return 3; } } while(0)
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    int negative=argc==2&&!strcmp(argv[1],"--negative-missing-socket");
    if(argc>1&&!negative) return 64;
    if(safe_setup()) { puts("PROBE_SETUP_FAILED=synthetic_token_or_config_missing"); return 65; }
    const char *server=negative?"unix:/tmp/linpad-pulse-probe/no-such-server":"unix:/tmp/ishaudio/native";
    printf("PULSE_PROBE_VERSION=1\nSERVER=%s\nNOAUTOSPAWN=1\nAUDIO_STREAM_CREATED=0\n",server);
    double start=now();
    void *lib=dlopen("libpulse.so.0",RTLD_NOW|RTLD_LOCAL);
    if(!lib) { printf("DLOPEN_FAILED=%s\n",dlerror()); return 2; }
    puts("LIBPULSE_LOAD_PASS=1");
    LOAD(mainloop_new); LOAD(mainloop_free); LOAD(mainloop_get_api); LOAD(mainloop_iterate);
    LOAD(context_new); LOAD(context_unref); LOAD(context_connect); LOAD(context_disconnect);
    LOAD(context_get_state); LOAD(context_errno); LOAD(context_get_sink_info_by_name);
    LOAD(operation_unref); LOAD(strerror);
    pa_mainloop *loop=p_mainloop_new();
    if(!loop) { puts("MAINLOOP_NULL=1"); return 4; }
    pa_context *ctx=p_context_new(p_mainloop_get_api(loop),"linpad-audio-route-probe");
    if(!ctx) { puts("CONTEXT_NULL=1"); return 5; }
    puts("CONTEXT_CREATED=1");
    int rc=p_context_connect(ctx,server,1,NULL),last=-1,ready=0;
    double deadline=now()+15.0;
    while(rc>=0&&now()<deadline) {
        int s=p_context_get_state(ctx);
        if(s!=last) { printf("CONTEXT_STATE=%d\n",s); last=s; }
        if(s==4) { ready=1; break; }
        if(s==5||s==6) break;
        rc=p_mainloop_iterate(loop,0,NULL);
        struct timespec nap={0,10000000}; nanosleep(&nap,NULL);
    }
    int result=6;
    pa_operation *op=NULL;
    if(ready) {
        puts("PULSE_CONNECT_PASS=1");
        struct state state={0};
        op=p_context_get_sink_info_by_name(ctx,"ishaudio",sink_cb,&state);
        deadline=now()+10.0;
        while(op&&!state.done&&now()<deadline) {
            if(p_mainloop_iterate(loop,0,NULL)<0) break;
            struct timespec nap={0,10000000}; nanosleep(&nap,NULL);
        }
        printf("ISHAUDIO_SINK_COUNT=%d\nSINK_QUERY_DONE=%d\nSINK_QUERY_FAILED=%d\n",state.found,state.done,state.failed);
        if(state.done&&!state.failed&&state.found==1) { puts("PULSE_ROUTE_PASS=1"); result=0; }
        else result=7;
    } else {
        int error=p_context_errno(ctx);
        printf("PULSE_CONNECT_FAILED=1\nPULSE_ERRNO=%d\nPULSE_ERROR=%s\n",error,p_strerror(error));
        if(negative&&p_context_get_state(ctx)==5) { puts("NEGATIVE_MISSING_SOCKET_PASS=1"); result=0; }
    }
    if(op) p_operation_unref(op);
    p_context_disconnect(ctx); p_context_unref(ctx); p_mainloop_free(loop); dlclose(lib);
    printf("PULSE_PROBE_ELAPSED=%.3f\nPULSE_PROBE_EXIT=%d\n",now()-start,result);
    return result;
}
