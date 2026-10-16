/* Where does this emulator put the things that decide a process's mmap base?
 * glibc derives its mmap_base from the stack top, so the stack position decides whether shared
 * libraries land below or above 4 GB -- and that decides whether wine can keep its 32-bit view at
 * base 0 (guest address == host address) or has to offset it. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void) {
    int local = 0;
    void *m = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("stack-ish (address of a local): %p\n", (void *) &local);
    printf("sbrk(0)                       : %p\n", sbrk(0));
    printf("fresh mmap(1 MB)              : %p\n", m);
    printf("libc function address         : %p\n", (void *) (void (*)(void)) printf);
    printf("main                          : %p\n", (void *) (void (*)(void)) main);
    printf("4 GB mark                     : 0x100000000\n");
    unsigned long below = 0;
    below += ((unsigned long) &local) < 0x100000000UL;
    below += ((unsigned long) m) < 0x100000000UL;
    below += ((unsigned long) (void *) printf) < 0x100000000UL;
    printf("of those three, below 4 GB    : %lu\n", below);
    return 0;
}
