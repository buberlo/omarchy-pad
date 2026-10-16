// Temporary per-command diagnostic for FEX's low-address munmap length check.
// Linux x86 pages are 4096 bytes. No address rounding and no failure suppression.
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
int munmap(void *address, size_t length) {
    if (!length || length>SIZE_MAX-4095) { errno=EINVAL; return -1; }
    return (int)syscall(SYS_munmap,address,(length+4095)&~(size_t)4095);
}
