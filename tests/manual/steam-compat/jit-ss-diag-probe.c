// AArch64 Linux: exercise a known native-JIT fallback, then read its opt-in proc snapshot.
// argv[1] is "on" only when the host was launched with ISH_JIT_SS_DIAG=1.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

__attribute__((naked, noinline)) static void negative_entry(uint64_t fpcr) {
    __asm__ volatile("msr fpcr, x0\nret");
}

int main(int argc, char **argv) {
    int enabled = argc > 1 && strcmp(argv[1], "on") == 0;
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    for (unsigned i = 0; i < 100000; i++) {
        __asm__ volatile("msr fpcr, %0" :: "r"(fpcr));
        negative_entry(fpcr);
    }
    FILE *f = fopen("/proc/ish/jit-ss", "r");
    if (!f) { perror("jit-ss"); return 1; }
    char *text = calloc(1, 1024 * 1024);
    if (!text) return 2;
    size_t n = fread(text, 1, 1024 * 1024 - 1, f);
    if (ferror(f) || n == 1024 * 1024 - 1) return 3;
    fclose(f);
    fputs(text, stdout);
    int ok = strstr(text, enabled ? "enabled 1\n" : "enabled 0\n") != NULL;
    if (enabled) {
        ok = ok && strstr(text, "truncated 0\n");
        unsigned own = (unsigned)getpid();
        int own_context = 0, own_opcode = 0;
        char *state;
        for (char *line = strtok_r(text, "\n", &state); line; line = strtok_r(NULL, "\n", &state)) {
            unsigned tid, tgid;
            if (sscanf(line, "context tid=%u tgid=%u ", &tid, &tgid) == 2 && tid == own && tgid == own) {
                char *hits = strstr(line, " hit=");
                char *neg = strstr(line, " path_negative=");
                char *exit = strstr(line, " path_exit=");
                own_context = hits && neg && exit && strtoull(hits + 5, NULL, 10) > 100000 &&
                    strtoull(neg + 15, NULL, 10) > 0 && strtoull(exit + 11, NULL, 10) > 0;
            }
            unsigned long long mmu, serial, pc, insn, readable, path, miss;
            if (sscanf(line, "sample tid=%u mmu=%llu serial=%llu pc=%llx insn=%llx readable=%llu path=%llu miss=%llu",
                       &tid, &mmu, &serial, &pc, &insn, &readable, &path, &miss) == 8 && tid == own &&
                    readable == 1 && (insn & 0xffffffe0ull) == 0xd51b4400ull)
                own_opcode = 1;
        }
        ok = ok && own_context && own_opcode;
    } else {
        ok = ok && !strstr(text, "context tid=");
    }
    free(text);
    printf("SS_DIAG_PROC %s enabled=%d\n", ok ? "PASS" : "FAIL", enabled);
    return ok ? 0 : 4;
}
