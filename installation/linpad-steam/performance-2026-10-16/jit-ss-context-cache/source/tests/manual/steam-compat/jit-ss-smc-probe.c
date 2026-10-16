// ARM64 guest regression for invalidating cached single-step fallback code.
// MSR FPCR is intentionally handled by the gadget fallback (translate.c),
// while MRS FPCR and RET can use native translations. Alternate the source
// register of the same fallback instruction and verify no stale version runs.
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

typedef uint64_t (*function_t)(uint64_t, uint64_t);

int main(void) {
    uint64_t saved;
    __asm__ volatile("mrs %0, fpcr" : "=r"(saved));
    uint64_t first = saved & ~(UINT64_C(3) << 22);
    uint64_t second = first | (UINT64_C(1) << 22);
    uint32_t *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) return 2;
    code[1] = 0xd53b4400; // mrs x0, fpcr
    code[2] = 0xd65f03c0; // ret
    unsigned bad = 0;
    for (unsigned i = 0; i < 200; i++) {
        code[0] = 0xd51b4400 | (i & 1); // msr fpcr, x0 or x1
        __builtin___clear_cache((char *)code, (char *)(code + 3));
        uint64_t actual = ((function_t)code)(first, second);
        uint64_t expected = (i & 1) ? second : first;
        if (actual != expected) bad++;
    }
    __asm__ volatile("msr fpcr, %0" :: "r"(saved));
    munmap(code, 4096);
    printf("jit-ss-smc %s checks=200 failures=%u\n", bad ? "FAIL" : "PASS", bad);
    return bad != 0;
}
