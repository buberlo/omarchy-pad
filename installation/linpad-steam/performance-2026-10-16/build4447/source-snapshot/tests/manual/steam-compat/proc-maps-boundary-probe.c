#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

// Native AArch64/Linux probe. High addresses reproduce FEX's allocation layout;
// separate 2 MiB page-table subtrees expose a sparse walk across a real hole.
static const uintptr_t island = UINT64_C(0x8000101ff000);
static const uintptr_t second = UINT64_C(0x8000105ff000);
static const uintptr_t stack_like = UINT64_C(0x808007800000);
static int failures, checks;

static void map_at(uintptr_t address, size_t length) {
    void *p = mmap((void *)address, length, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != (void *)address) {
        fprintf(stderr, "SETUP_FAIL mmap expected=%" PRIxPTR " actual=%p errno=%d\n",
                address, p, errno);
        exit(2);
    }
    *(volatile unsigned char *)p = 42;
}

static void check(uintptr_t address, uintptr_t expected_start,
                  uintptr_t expected_end, const char *label) {
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) { perror("maps"); exit(2); }
    char line[1024];
    uintptr_t start = 0, end = 0;
    int found = 0;
    while (fgets(line, sizeof(line), maps)) {
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR, &start, &end) == 2 &&
            address >= start && address < end) {
            found = 1;
            break;
        }
    }
    fclose(maps);
    int pass = expected_end ? found && start == expected_start && end == expected_end : !found;
    checks++;
    if (!pass) failures++;
    printf("%s %s address=%" PRIxPTR " found=%d", pass ? "PASS" : "FAIL", label, address, found);
    if (found) printf(" actual=%" PRIxPTR "-%" PRIxPTR, start, end);
    printf(" expected=%" PRIxPTR "-%" PRIxPTR "\n", expected_start, expected_end);
}

int main(void) {
    alarm(15);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (sysconf(_SC_PAGESIZE) != 4096) {
        fputs("SETUP_FAIL requires 4 KiB Linux guest pages\n", stderr);
        return 2;
    }
    map_at(island, 4096);
    map_at(second, 4096);
    map_at(stack_like, 8 * 1024 * 1024);
    check(island, island, island + 4096, "first sparse island");
    check(second, second, second + 4096, "second sparse island");
    check(island + 2 * 1024 * 1024, 0, 0, "hole between anonymous islands");
    check(stack_like, stack_like, stack_like + 8 * 1024 * 1024, "final high stack-like mapping");
    check(stack_like + 8 * 1024 * 1024, 0, 0, "first byte beyond final mapping");

    map_at(island + 4096, 4096);
    check(island, island, island + 8192, "adjacent anonymous mappings still coalesce");
    if (mprotect((void *)island, 4096, PROT_READ)) { perror("mprotect"); return 2; }
    check(island, island, island + 4096, "permission boundary first page");
    check(island + 4096, island + 4096, island + 8192, "permission boundary second page");

    if (munmap((void *)stack_like, 8 * 1024 * 1024)) { perror("munmap"); return 2; }
    check(stack_like, 0, 0, "unmapped final region absent");
    check(second, second, second + 4096, "new final mapping ends exactly");
    munmap((void *)island, 8192);
    munmap((void *)second, 4096);
    printf("PROC_MAPS_BOUNDARY checks=%d failures=%d\n", checks, failures);
    return failures != 0;
}
