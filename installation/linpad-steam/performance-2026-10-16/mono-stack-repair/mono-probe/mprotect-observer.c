#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

/* Diagnostic only; forwards exact arguments and return/errno unchanged.
 * Called in normal guest code, not from a signal handler. The introspection
 * calls may change timing and are not suitable for a performance benchmark. */
static int (*next_mprotect)(void *, size_t, int);
static __thread int inside;
static void stack_note(const char *phase) {
    struct rlimit limit = {0};
    int limit_rc = getrlimit(RLIMIT_STACK, &limit);
    pthread_attr_t attr;
    void *base = NULL;
    size_t bytes = 0;
    int local;
    int rc = pthread_getattr_np(pthread_self(), &attr), get_rc = -1;
    if (!rc) {
        get_rc = pthread_attr_getstack(&attr, &base, &bytes);
        pthread_attr_destroy(&attr);
    }
    char out[256];
    int n = snprintf(out, sizeof(out), "MPROTECT_OBSERVER STACK phase=%s local=%p getattr=%d getstack=%d base=%p size=0x%zx\n",
                     phase, (void *)&local, rc, get_rc, base, bytes);
    if (n > 0) write(2, out, (size_t)n < sizeof(out) ? (size_t)n : sizeof(out)-1);
    n = snprintf(out, sizeof(out), "MPROTECT_OBSERVER RLIMIT_STACK phase=%s result=%d soft=0x%llx hard=0x%llx\n",
                 phase, limit_rc, (unsigned long long)limit.rlim_cur, (unsigned long long)limit.rlim_max);
    if (n > 0) write(2, out, (size_t)n < sizeof(out) ? (size_t)n : sizeof(out)-1);
}
__attribute__((constructor)) static void init(void) {
    next_mprotect = dlsym(RTLD_NEXT, "mprotect");
    inside = 1;
    stack_note("startup");
    inside = 0;
}
int mprotect(void *address, size_t bytes, int protection) {
    if (!next_mprotect) next_mprotect = dlsym(RTLD_NEXT, "mprotect");
    if (!next_mprotect) { errno = ENOSYS; return -1; }
    if (inside || (protection != PROT_NONE && (uintptr_t)address < UINT64_C(0x800000000000)))
        return next_mprotect(address, bytes, protection);
    int saved_errno = errno;
    inside = 1;
    void *caller = __builtin_return_address(0);
    Dl_info info = {0};
    int found = dladdr(caller, &info);
    char out[512];
    int n = snprintf(out, sizeof(out), "MPROTECT_OBSERVER CALL addr=%p len=0x%zx prot=%d caller=%p module=%s base=%p offset=0x%lx\n",
                     address, bytes, protection, caller, found && info.dli_fname ? info.dli_fname : "?",
                     found ? info.dli_fbase : NULL, found ? (unsigned long)((uintptr_t)caller-(uintptr_t)info.dli_fbase) : 0);
    if (n > 0) write(2, out, (size_t)n < sizeof(out) ? (size_t)n : sizeof(out)-1);
    stack_note("before-call");
    inside = 0;
    errno = saved_errno;
    int rc = next_mprotect(address, bytes, protection);
    saved_errno = errno;
    n = snprintf(out, sizeof(out), "MPROTECT_OBSERVER RETURN addr=%p len=0x%zx prot=%d result=%d errno=%d\n",
                 address, bytes, protection, rc, saved_errno);
    if (n > 0) write(2, out, (size_t)n < sizeof(out) ? (size_t)n : sizeof(out)-1);
    errno = saved_errno;
    return rc;
}
