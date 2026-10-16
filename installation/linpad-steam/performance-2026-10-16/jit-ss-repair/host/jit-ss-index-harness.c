
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

struct ss_entry {
    addr_t pc;
    struct fiber_block *block;
    struct ss_entry *next;
    struct ss_entry **hash_prev;      // link pointing here in the fixed-size PC hash
    struct ss_entry *page_next;
};

static inline size_t pc_hash(addr_t pc, size_t size) {
    return (size_t) ((pc * 0x9e3779b97f4a7c15ull) >> 32) & (size - 1);
}

static void ss_insert_locked(struct jit_mm *mm, struct ss_entry *e) {
    size_t h = pc_hash(e->pc, mm->ss_size);
    e->next = mm->ss_hash[h];
    e->hash_prev = &mm->ss_hash[h];
    if (e->next)
        e->next->hash_prev = &e->next;
    mm->ss_hash[h] = e;
    size_t bucket = PAGE(e->pc) % mm->page_hash_size;
    e->page_next = mm->ss_page_hash[bucket];
    mm->ss_page_hash[bucket] = e;
    mm->ss_count++;
    __atomic_fetch_or(&mm->ss_bloom, 1ull << (PAGE(e->pc) & 63), __ATOMIC_RELEASE);
}

static bool ss_invalidate_locked(struct jit_mm *mm, page_t start, page_t end) {
    bool did = false;
    // Writes to data pages often collide with the coarse 64-bit bloom filter.
    // Only visit their page buckets, not every cached fallback instruction.
    size_t buckets = end - start < mm->page_hash_size ? end - start : mm->page_hash_size;
    for (size_t i = 0; i < buckets; i++) {
        size_t bucket = (start + i) % mm->page_hash_size;
        struct ss_entry **pp = &mm->ss_page_hash[bucket];
        while (*pp) {
            struct ss_entry *e = *pp;
            if (PAGE(e->pc) >= start && PAGE(e->pc) < end) {
                *pp = e->page_next;
                *e->hash_prev = e->next;
                if (e->next)
                    e->next->hash_prev = e->hash_prev;
                if (--mm->ss_count == 0)
                    __atomic_store_n(&mm->ss_bloom, 0, __ATOMIC_RELEASE);
                e->next = mm->ss_jetsam;
                mm->ss_jetsam = e;
                did = true;
            } else {
                pp = &e->page_next;
            }
        }
    }
    return did;
}

void jit_invalidate_range(struct jit_mm *mm, page_t start, page_t end) {
    if (mm == NULL || mm_dying(mm))
        return;
    if (end - start <= 64) {
        bool any = false;
        for (page_t p = start; p < end && !any; p++)
        {
            // exact unless the bucket holds blocks of several pages
            uint64_t bp = __atomic_load_n(&mm->bucket_page[p % mm->page_hash_size], __ATOMIC_ACQUIRE);
            any = bp == ~0ull || bp == p + 1;
        }
        uint64_t bloom = __atomic_load_n(&mm->ss_bloom, __ATOMIC_RELAXED);
        for (page_t p = start; p < end && !any && bloom; p++)
            any = (bloom >> (p & 63)) & 1;
        if (!any)
            return;
    }
    pthread_mutex_lock(&mm->lock);
    uint64_t t0 = jit_stats ? clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID) : 0;
    bool did = false;
    if (end - start > mm->page_hash_size) {
        for (size_t i = 0; i < mm->page_hash_size; i++) {
            struct jit_block *b = mm->page_hash[i];
            while (b) {
                struct jit_block *n = b->page_next;
                if (PAGE(b->pc) >= start && PAGE(b->pc) < end) {
                    invalidate_block_locked(mm, b);
                    did = true;
                }
                b = n;
            }
        }
    } else {
        jit_write_begin();
        for (page_t p = start; p < end; p++) {
            size_t bucket = p % mm->page_hash_size;
            struct jit_block **pp = &mm->page_hash[bucket];
            while (*pp) {
                struct jit_block *b = *pp;
                if (PAGE(b->pc) == p) {
                    *pp = b->page_next;
                    invalidate_block_nosync(mm, b);
                    did = true;
                } else {
                    pp = &b->page_next;
                }
            }
            if (mm->page_hash[bucket] == NULL)
                __atomic_store_n(&mm->bucket_page[bucket], 0, __ATOMIC_RELEASE);
        }
        patch_flush();
        jit_write_finish();
    }
    did |= ss_invalidate_locked(mm, start, end);
    if (did) {
        atomic_fetch_add(&mm->gen, 1);
        mm->stats_inval++;
    }
    if (jit_stats)
        mm->stats_inval_ns += clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID) - t0;
    pthread_mutex_unlock(&mm->lock);
}

static struct fiber_block *ss_get(struct jit_mm *mm, addr_t pc, struct tlb *tlb, bool keep) {
    size_t h = pc_hash(pc, mm->ss_size);
    pthread_mutex_lock(&mm->lock);
    for (struct ss_entry *e = mm->ss_hash[h]; e; e = e->next) {
        if (e->pc == pc) {
            struct fiber_block *b = e->block;
            pthread_mutex_unlock(&mm->lock);
            return b;
        }
    }
    uint64_t gen = atomic_load(&mm->gen);
    pthread_mutex_unlock(&mm->lock);

    struct gen_state state;
    gen_start(pc, &state);
    gen_step(&state, tlb);
    gen_exit(&state);
    gen_end(&state);
    struct fiber_block *block = state.block;

    pthread_mutex_lock(&mm->lock);
    struct ss_entry *e = mem_host_alloc_fails() ? NULL : malloc(sizeof(*e));
    if (e == NULL) {
        // Out of host memory: the block cannot be tracked, so it cannot be
        // freed later either. It is still valid for this one step.
        mm->stats_fallback++;
        pthread_mutex_unlock(&mm->lock);
        return block;
    }
    e->pc = pc;
    e->block = block;
    if ((keep || mm->ss_count < SS_NOMEM_CACHED) && atomic_load(&mm->gen) == gen) {
        ss_insert_locked(mm, e);
    } else {
        e->next = mm->ss_jetsam;   // used once, freed by reclaim
        mm->ss_jetsam = e;
    }
    mm->stats_fallback++;
    pthread_mutex_unlock(&mm->lock);
    return block;
}
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
