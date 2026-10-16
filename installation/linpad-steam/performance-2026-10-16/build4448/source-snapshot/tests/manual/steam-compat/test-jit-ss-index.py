#!/usr/bin/env python3
"""Run the actual private JIT cache functions with a small host fixture.

Extraction avoids a public test API or linking the complete Darwin runtime.
Only translation and compiled-block machinery are stubbed; cache operations,
range filtering, generation changes, and ss_get publication are production C.
The benchmark retains the prior full-scan implementation as its control.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def declaration(source, needle):
    start = source.index(needle)
    opening = source.index("{", start)
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    if source[cursor:cursor + 1] == ";":
        cursor += 1
    return source[start:cursor]


PREAMBLE = r'''
#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define clock_gettime_nsec_np fixture_clock_ns
typedef uint64_t addr_t;
typedef uint64_t page_t;
#define PAGE(x) ((x) >> 12)
#define BAD_PAGE (UINT64_C(1) << 36)
#define SS_NOMEM_CACHED 8192
struct fiber_block { int unused; };
struct tlb { int unused; };
struct ss_entry;
struct jit_block { addr_t pc; struct jit_block *page_next; };
struct jit_mm {
    struct ss_entry **ss_hash, **ss_page_hash;
    size_t ss_size, ss_count, page_hash_size;
    uint64_t ss_bloom, *bucket_page;
    struct ss_entry *ss_jetsam;
    struct jit_block **page_hash;
    pthread_mutex_t lock;
    _Atomic uint64_t gen;
    uint64_t stats_inval, stats_inval_ns, stats_fallback;
};
static bool jit_stats, allocation_fails;
static bool mm_dying(struct jit_mm *mm) { (void) mm; return false; }
static void jit_write_begin(void) {}
static void jit_write_finish(void) {}
static void patch_flush(void) {}
static void invalidate_block_locked(struct jit_mm *m, struct jit_block *b) {
    (void) m; (void) b; abort();
}
static void invalidate_block_nosync(struct jit_mm *m, struct jit_block *b) {
    (void) m; (void) b; abort();
}
static uint64_t clock_gettime_nsec_np(clockid_t clock) {
    struct timespec t; assert(clock_gettime(clock, &t) == 0);
    return (uint64_t)t.tv_sec * 1000000000 + (uint64_t)t.tv_nsec;
}
static bool mem_host_alloc_fails(void) { return allocation_fails; }
struct gen_state { struct fiber_block *block; };
static struct fiber_block dummy_block;
static struct jit_mm *invalidate_during_translation;
static page_t invalidation_page;
void jit_invalidate_range(struct jit_mm *, page_t, page_t);
static void gen_start(addr_t pc, struct gen_state *s) {
    (void) pc; s->block = &dummy_block;
}
static void gen_step(struct gen_state *s, struct tlb *tlb) {
    (void) s; (void) tlb;
    if (invalidate_during_translation) {
        struct jit_mm *mm = invalidate_during_translation;
        invalidate_during_translation = NULL;
        jit_invalidate_range(mm, invalidation_page, invalidation_page + 1);
    }
}
static void gen_exit(struct gen_state *s) { (void) s; }
static void gen_end(struct gen_state *s) { (void) s; }
'''

HARNESS = r'''
static unsigned checks;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); abort(); } checks++; } while(0)
static void init(struct jit_mm *mm) {
    memset(mm, 0, sizeof(*mm));
    mm->ss_size = 256; mm->page_hash_size = 65536;
    mm->ss_hash = calloc(mm->ss_size, sizeof(*mm->ss_hash));
    mm->ss_page_hash = calloc(mm->page_hash_size, sizeof(*mm->ss_page_hash));
    mm->page_hash = calloc(mm->page_hash_size, sizeof(*mm->page_hash));
    mm->bucket_page = calloc(mm->page_hash_size, sizeof(*mm->bucket_page));
    assert(mm->ss_hash && mm->ss_page_hash && mm->page_hash && mm->bucket_page);
    assert(pthread_mutex_init(&mm->lock, NULL) == 0);
}
static void destroy(struct jit_mm *mm) {
    for (size_t i = 0; i < mm->ss_size; i++) {
        struct ss_entry *e = mm->ss_hash[i];
        while (e) { struct ss_entry *next = e->next; free(e); e = next; }
    }
    struct ss_entry *e = mm->ss_jetsam;
    while (e) { struct ss_entry *next = e->next; free(e); e = next; }
    free(mm->ss_hash); free(mm->ss_page_hash);
    free(mm->page_hash); free(mm->bucket_page);
    pthread_mutex_destroy(&mm->lock);
}
static void add(struct jit_mm *mm, page_t page, unsigned offset) {
    struct ss_entry *e = calloc(1, sizeof(*e)); assert(e);
    e->pc = (page << 12) + offset;
    e->block = &dummy_block;
    pthread_mutex_lock(&mm->lock);
    ss_insert_locked(mm, e);
    pthread_mutex_unlock(&mm->lock);
}
static bool contains(struct jit_mm *mm, addr_t pc) {
    for (struct ss_entry *e = mm->ss_hash[pc_hash(pc, mm->ss_size)]; e; e = e->next)
        if (e->pc == pc) return true;
    return false;
}
static void validate(struct jit_mm *mm) {
    size_t pcs = 0, pages = 0;
    for (size_t i = 0; i < mm->ss_size; i++) {
        struct ss_entry **link = &mm->ss_hash[i];
        while (*link) {
            struct ss_entry *e = *link;
            assert(e->hash_prev == link && pc_hash(e->pc, mm->ss_size) == i);
            assert((mm->ss_bloom >> (PAGE(e->pc) & 63)) & 1);
            unsigned occurrences = 0;
            for (struct ss_entry *p = mm->ss_page_hash[PAGE(e->pc) % mm->page_hash_size]; p; p = p->page_next)
                occurrences += p == e;
            assert(occurrences == 1);
            pcs++; link = &e->next;
        }
    }
    for (size_t i = 0; i < mm->page_hash_size; i++)
        for (struct ss_entry *e = mm->ss_page_hash[i]; e; e = e->page_next) {
            assert(PAGE(e->pc) % mm->page_hash_size == i);
            assert(contains(mm, e->pc)); pages++;
        }
    assert(pcs == mm->ss_count && pages == pcs);
    if (!pcs) assert(mm->ss_bloom == 0);
}
static size_t retired_count(struct jit_mm *mm) {
    size_t count = 0;
    for (struct ss_entry *e = mm->ss_jetsam; e; e = e->next) count++;
    return count;
}
static void focused_tests(void) {
    struct jit_mm m; init(&m);
    const page_t p = UINT64_C(0x80012345), collision = p + 65536;
    add(&m, p, 0); add(&m, p, 4); add(&m, p, 4092);
    add(&m, p - 1, 4092); add(&m, p + 1, 0); add(&m, collision, 0);
    validate(&m);
    jit_invalidate_range(&m, p, p);
    CHECK(m.ss_count == 6 && atomic_load(&m.gen) == 0);
    jit_invalidate_range(&m, p, p + 1);
    validate(&m);
    CHECK(m.ss_count == 3 && retired_count(&m) == 3);
    CHECK(contains(&m, (p - 1) << 12 | 4092));
    CHECK(contains(&m, (p + 1) << 12));
    CHECK(contains(&m, collision << 12));
    CHECK(atomic_load(&m.gen) == 1 && m.stats_inval == 1);
    jit_invalidate_range(&m, p, p + 1);
    CHECK(atomic_load(&m.gen) == 1);
    // Wrap around the page-bucket array and include the exact end boundary.
    add(&m, 65535, 0); add(&m, 65536, 0); add(&m, 65537, 0);
    jit_invalidate_range(&m, 65535, 65537);
    CHECK(!contains(&m, UINT64_C(65535) << 12));
    CHECK(!contains(&m, UINT64_C(65536) << 12));
    CHECK(contains(&m, UINT64_C(65537) << 12));
    jit_invalidate_range(&m, 0, 65536 * 3);
    validate(&m);
    CHECK(m.ss_count == 3); // all three high-address entries remain
    jit_invalidate_range(&m, 0, BAD_PAGE);
    validate(&m);
    CHECK(m.ss_count == 0 && retired_count(&m) == 9 && m.ss_bloom == 0);
    destroy(&m);

    // Actual ss_get publication path, including a generation change while its
    // generated instruction is outside mm->lock (deterministic interleaving).
    init(&m);
    add(&m, p, 0);
    invalidation_page = p; invalidate_during_translation = &m;
    CHECK(ss_get(&m, p << 12 | 4, NULL, true) == &dummy_block);
    CHECK(atomic_load(&m.gen) == 1 && m.ss_count == 0 && retired_count(&m) == 2);
    validate(&m);
    allocation_fails = true;
    CHECK(ss_get(&m, p << 12 | 8, NULL, true) == &dummy_block);
    CHECK(m.ss_count == 0 && retired_count(&m) == 2);
    allocation_fails = false;
    for (unsigned i = 0; i < SS_NOMEM_CACHED; i++) add(&m, 100 + i, 0);
    CHECK(ss_get(&m, (p + 2) << 12, NULL, false) == &dummy_block);
    CHECK(m.ss_count == SS_NOMEM_CACHED && retired_count(&m) == 3);
    CHECK(ss_get(&m, (p + 2) << 12 | 4, NULL, true) == &dummy_block);
    CHECK(m.ss_count == SS_NOMEM_CACHED + 1);
    CHECK(ss_get(&m, (p + 2) << 12 | 4, NULL, true) == &dummy_block);
    CHECK(m.ss_count == SS_NOMEM_CACHED + 1);
    validate(&m);
    jit_invalidate_range(&m, 0, BAD_PAGE);
    validate(&m);
    CHECK(retired_count(&m) == SS_NOMEM_CACHED + 4);
    destroy(&m);
}
static uint64_t random_state = 42;
static uint32_t random32(void) {
    random_state = random_state * UINT64_C(6364136223846793005) + 1;
    return random_state >> 32;
}
static void randomized_tests(void) {
    enum { N = 2000 };
    addr_t pc[N]; bool alive[N]; struct jit_mm m; init(&m);
    for (unsigned i = 0; i < N; i++) {
        page_t p = UINT64_C(0x80000000) + random32() % (65536 * 4);
        pc[i] = p << 12 | (i % 1024) * 4;
        // Ensure unique PCs without weakening hash/page collisions.
        while (contains(&m, pc[i])) pc[i] += 4096;
        alive[i] = true; add(&m, PAGE(pc[i]), pc[i] & 4095);
    }
    for (unsigned round = 0; round < 120; round++) {
        page_t start = UINT64_C(0x80000000) + random32() % (65536 * 4);
        page_t end = start + (round % 8 == 0 ? 65536 + random32() % 1000 : random32() % 80);
        bool did = false;
        for (unsigned i = 0; i < N; i++)
            if (alive[i] && PAGE(pc[i]) >= start && PAGE(pc[i]) < end) { alive[i] = false; did = true; }
        uint64_t before = atomic_load(&m.gen);
        jit_invalidate_range(&m, start, end);
        CHECK(atomic_load(&m.gen) == before + did);
        for (unsigned i = 0; i < N; i++) CHECK(contains(&m, pc[i]) == alive[i]);
        validate(&m);
    }
    jit_invalidate_range(&m, 0, BAD_PAGE); validate(&m);
    CHECK(retired_count(&m) == N);
    destroy(&m);
}
// Original full-cache walk, retained verbatim except the function name.
static void old_ss_invalidate_locked(struct jit_mm *mm, page_t start, page_t end) {
    for (size_t i = 0; i < mm->ss_size; i++) {
        struct ss_entry **pp = &mm->ss_hash[i];
        while (*pp) {
            struct ss_entry *e = *pp;
            if (PAGE(e->pc) >= start && PAGE(e->pc) < end) {
                *pp = e->next;
                if (--mm->ss_count == 0)
                    __atomic_store_n(&mm->ss_bloom, 0, __ATOMIC_RELEASE);
                e->next = mm->ss_jetsam;
                mm->ss_jetsam = e;
            } else {
                pp = &e->next;
            }
        }
    }
}
static void benchmark(void) {
    const unsigned entries = 65536, rounds = 1000;
    struct jit_mm m; init(&m);
    for (unsigned i = 0; i < entries; i++) add(&m, UINT64_C(0x80000000) + i, 0);
    // Same page-hash and bloom buckets, but a distinct high guest page:
    // neither implementation may remove any node.
    uint64_t begin = clock_gettime_nsec_np(CLOCK_MONOTONIC);
    for (unsigned i = 0; i < rounds; i++) {
        page_t p = UINT64_C(0x80000000) + 65536 + i;
        old_ss_invalidate_locked(&m, p, p + 1);
    }
    uint64_t old_ns = clock_gettime_nsec_np(CLOCK_MONOTONIC) - begin;
    begin = clock_gettime_nsec_np(CLOCK_MONOTONIC);
    for (unsigned i = 0; i < rounds; i++) {
        page_t p = UINT64_C(0x80000000) + 65536 + i;
        bool did = ss_invalidate_locked(&m, p, p + 1);
        assert(!did);
    }
    uint64_t new_ns = clock_gettime_nsec_np(CLOCK_MONOTONIC) - begin;
    validate(&m);
    printf("BENCH entries=%u invalidations=%u old_ns=%" PRIu64 " indexed_ns=%" PRIu64
           " old_node_visits=%" PRIu64 " indexed_node_visits=%u survivors=%zu\n",
           entries, rounds, old_ns, new_ns, (uint64_t)entries * rounds, rounds, m.ss_count);
    destroy(&m);
}
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--benchmark") == 0) { benchmark(); return 0; }
    focused_tests(); randomized_tests();
    printf("PASS actual production cache functions: %u assertions; index integrity after every range; ASan/UBSan enabled\n", checks);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--cc", default="clang")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    source_path = root / "jit/jit.c"
    source = source_path.read_text()
    needles = ["struct ss_entry {", "static inline size_t pc_hash(",
               "static void ss_insert_locked(", "static bool ss_invalidate_locked(",
               "void jit_invalidate_range(", "static struct fiber_block *ss_get("]
    snippets = [declaration(source, needle) for needle in needles]
    args.out.mkdir(parents=True, exist_ok=True)
    generated = args.out / "jit-ss-index-harness.c"
    generated.write_text(PREAMBLE + "\n" + "\n\n".join(snippets) + HARNESS)
    commands = []
    for name, flags, runtime_args in [
        ("regression", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"], []),
        ("benchmark", [], ["--benchmark"]),
    ]:
        binary = args.out / name
        command = [args.cc, "-O2", "-g", "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-pthread", *flags, str(generated), "-o", str(binary)]
        commands.append(command)
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary), *runtime_args], text=True, capture_output=True)
        (args.out / (name + ".txt")).write_text(result.stdout + result.stderr)
        print(result.stdout + result.stderr, end="")
        result.check_returncode()
    (args.out / "provenance.json").write_text(json.dumps({
        "source": "jit/jit.c", "source_sha256": hashlib.sha256(source_path.read_bytes()).hexdigest(),
        "extracted_declarations": needles, "commands": commands,
        "scope": "Local host fixture runs actual production private functions; no device/gameplay performance claim.",
        "stubs": "Translation, compiled-block removal, lifetime teardown and allocator-failure injection. No compiled blocks inserted.",
        "benchmark": "65536 entries, 1000 false-positive page invalidations; original full scan versus actual new index."
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
