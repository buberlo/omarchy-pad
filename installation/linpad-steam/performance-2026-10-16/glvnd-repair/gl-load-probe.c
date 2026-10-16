#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    void *library = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library) { printf("LIBGL_LOAD_FAIL %s\n", dlerror()); return 3; }
    const char *names[] = { "glXGetProcAddressARB", "glXQueryVersion", "glGetString", NULL };
    for (unsigned i = 0; names[i]; ++i) {
        if (!dlsym(library, names[i])) { printf("LIBGL_SYMBOL_FAIL %s %s\n", names[i], dlerror()); return 4; }
        printf("LIBGL_SYMBOL_PASS %s\n", names[i]);
    }
    dlclose(library);
    puts("LIBGL_LOAD_PASS");
    return 0;
}
