
#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdarg.h>
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
#define JIT_SS_LC_SIZE 256
struct jit_ss_cache_entry {
    addr_t pc;
    struct fiber_block *block;
    uint64_t gen;
};
#define SS_DIAG_SAMPLES 64
#define SS_DIAG_CONTEXTS 128
enum ss_diag_counter {
    SD_HIT, SD_COLD, SD_COLLISION, SD_GENERATION, SD_GENERATION_SAME_PC,
    SD_SHARED_HIT, SD_FILL, SD_ONESHOT_GENERATION, SD_ONESHOT_PRESSURE,
    SD_UNTRACKED_OOM, SD_NEGATIVE, SD_EXIT, SD_NOMEM, SD_COUNTERS
};
enum ss_origin { SS_NEGATIVE, SS_EXIT, SS_NOMEM };
enum ss_result { SS_SHARED_HIT, SS_FILL, SS_ONESHOT_GENERATION, SS_ONESHOT_PRESSURE, SS_UNTRACKED_OOM };
struct ss_diag_sample {
    _Atomic uint64_t serial, pc, insn, readable, origin, miss;
};
struct jit_ss_diag {
    uint64_t owner_mmu;   // owner-only; proc readers never inspect this field
    uint32_t random;
    _Atomic uint64_t reset_seq, mmu_id, tid, tgid;
    _Atomic uint64_t counters[SD_COUNTERS], sampled;
    struct ss_diag_sample samples[SS_DIAG_SAMPLES];
};
struct ss_diag_snapshot {
    bool consistent;
    uint64_t reset_seq, mmu_id, tid, tgid, counters[SD_COUNTERS], sampled;
    struct { uint64_t serial, pc, insn, readable, origin, miss; } samples[SS_DIAG_SAMPLES];
    unsigned busy_samples;
};
struct fiber_block { int unused; };
struct tlb { bool readable; uint32_t insn; addr_t segfault_addr; };
static bool tlb_read(struct tlb *t, addr_t pc, void *out, unsigned size) {
    assert(size == 4); if (!t->readable) { t->segfault_addr = pc; return false; }
    memcpy(out, &t->insn, 4); return true;
}
struct mmu { uint64_t id, changes; };
struct ss_entry;
struct jit_block { addr_t pc; struct jit_block *page_next; };
struct jit_mm {
    struct mmu *mmu;
    struct ss_entry **ss_hash, **ss_page_hash;
    size_t ss_size, ss_count, ss_grow_at, page_hash_size;
    uint64_t ss_bloom, *bucket_page;
    struct ss_entry *ss_jetsam;
    struct jit_block **page_hash;
    pthread_mutex_t lock;
    _Atomic uint64_t gen;
    uint64_t stats_inval, stats_inval_ns, stats_fallback;
};
struct jit_ctx {
    struct jit_mm *mm;
    uint64_t mmu_id, mem_changes, lc_gen;
    void *lc[4];
    struct jit_ss_cache_entry ss_lc[JIT_SS_LC_SIZE];
    unsigned tlb_flushes;
    struct jit_ss_diag *ss_diag;
    struct jit_ctx *reg_next;
};
static pthread_mutex_t reg_lock = PTHREAD_MUTEX_INITIALIZER;
static struct jit_ctx *reg_head;
static _Atomic bool jit_ss_diag_enabled;
static _Atomic uint64_t ss_diag_alloc_failed;
struct proc_data { char text[1024 * 1024]; size_t used; };
static void proc_printf(struct proc_data *b, const char *format, ...) {
    va_list ap; va_start(ap, format);
    int n = vsnprintf(b->text+b->used, sizeof(b->text)-b->used, format, ap);
    va_end(ap); assert(n >= 0 && (size_t)n < sizeof(b->text)-b->used); b->used += n;
}
static void tlb_flush_ctx(struct jit_ctx *ctx) { ctx->tlb_flushes++; }
static _Thread_local unsigned mutex_lock_calls;
static int counted_mutex_lock(pthread_mutex_t *m) {
    mutex_lock_calls++;
    return pthread_mutex_lock(m);
}
#define pthread_mutex_lock counted_mutex_lock
static void (*after_unlock_hook)(void);
static int counted_mutex_unlock(pthread_mutex_t *m) {
    int result = pthread_mutex_unlock(m);
    if (after_unlock_hook) {
        void (*hook)(void) = after_unlock_hook;
        after_unlock_hook = NULL;
        hook();
    }
    return result;
}
#define pthread_mutex_unlock counted_mutex_unlock
static bool jit_stats, allocation_fails;
static unsigned allocation_checks, translations;
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
static bool mem_host_alloc_fails(void) { allocation_checks++; return allocation_fails; }
struct gen_state { struct fiber_block *block; };
static struct fiber_block dummy_block;
static struct fiber_block *generated_block = &dummy_block;
static struct jit_mm *invalidate_during_translation;
static page_t invalidation_page;
void jit_invalidate_range(struct jit_mm *, page_t, page_t);
static void gen_start(addr_t pc, struct gen_state *s) {
    (void) pc; translations++; s->block = generated_block;
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

static void ss_diag_begin(struct jit_ss_diag *d, uint64_t mmu_id, uint64_t tid, uint64_t tgid) {
    if (d->owner_mmu == mmu_id)
        return;
    atomic_fetch_add(&d->reset_seq, 1);   // odd: changing address-space identity
    d->owner_mmu = mmu_id;
    d->random = (uint32_t) (mmu_id ^ tid ^ 0x9e3779b9u) | 1;
    atomic_store(&d->mmu_id, mmu_id);
    atomic_store(&d->tid, tid);
    atomic_store(&d->tgid, tgid);
    for (unsigned i = 0; i < SD_COUNTERS; i++)
        atomic_store(&d->counters[i], 0);
    atomic_store(&d->sampled, 0);
    for (unsigned i = 0; i < SS_DIAG_SAMPLES; i++)
        atomic_store(&d->samples[i].serial, 0);
    atomic_fetch_add(&d->reset_seq, 1);
}

static void ss_diag_count(struct jit_ss_diag *d, enum ss_diag_counter counter) {
    atomic_fetch_add_explicit(&d->counters[counter], 1, memory_order_relaxed);
}

static void ss_diag_sample(struct jit_ss_diag *d, addr_t pc, struct tlb *tlb,
        enum ss_origin origin, enum ss_diag_counter miss) {
    // Pseudorandom sampling avoids fixed-stride aliasing with tight guest loops.
    uint32_t x = d->random;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    d->random = x;
    if ((x & 1023) != 0)
        return;
    uint32_t insn = 0;
    addr_t saved_fault = tlb->segfault_addr;
    bool readable = tlb_read(tlb, pc, &insn, sizeof(insn));
    tlb->segfault_addr = saved_fault;
    uint64_t serial = atomic_fetch_add_explicit(&d->sampled, 1, memory_order_relaxed) + 1;
    struct ss_diag_sample *s = &d->samples[(serial - 1) % SS_DIAG_SAMPLES];
    // All sample fields are seq_cst atomics. A reader accepting an unchanged
    // nonzero serial therefore cannot combine two ring-buffer publications.
    atomic_store(&s->serial, 0);
    atomic_store(&s->pc, pc);
    atomic_store(&s->insn, insn);
    atomic_store(&s->readable, readable);
    atomic_store(&s->origin, origin);
    atomic_store(&s->miss, miss);
    atomic_store(&s->serial, serial);
}

static void ss_diag_copy(struct jit_ss_diag *d, struct ss_diag_snapshot *out) {
    for (unsigned attempt = 0; attempt < 2; attempt++) {
        memset(out, 0, sizeof(*out));
        uint64_t seq = atomic_load(&d->reset_seq);
        if (seq & 1)
            continue;
        out->reset_seq = seq;
        out->mmu_id = atomic_load(&d->mmu_id);
        out->tid = atomic_load(&d->tid);
        out->tgid = atomic_load(&d->tgid);
        for (unsigned i = 0; i < SD_COUNTERS; i++)
            out->counters[i] = atomic_load(&d->counters[i]);
        out->sampled = atomic_load(&d->sampled);
        for (unsigned i = 0; i < SS_DIAG_SAMPLES; i++) {
            struct ss_diag_sample *s = &d->samples[i];
            uint64_t serial = atomic_load(&s->serial);
            if (!serial)
                continue;
            out->samples[i].pc = atomic_load(&s->pc);
            out->samples[i].insn = atomic_load(&s->insn);
            out->samples[i].readable = atomic_load(&s->readable);
            out->samples[i].origin = atomic_load(&s->origin);
            out->samples[i].miss = atomic_load(&s->miss);
            if (serial == atomic_load(&s->serial))
                out->samples[i].serial = serial;
            else
                out->busy_samples++;
        }
        if (seq == atomic_load(&d->reset_seq)) {
            out->consistent = true;
            return;
        }
    }
}

void jit_ss_diag_show(struct proc_data *buf) {
    static const char *names[SD_COUNTERS] = {
        "hit", "miss_cold", "miss_collision", "miss_generation", "miss_generation_same_pc",
        "shared_hit", "persistent_fill", "oneshot_generation", "oneshot_pressure",
        "untracked_oom", "path_negative", "path_exit", "path_nomem"
    };
    proc_printf(buf, "supported 1\nenabled %d\n", atomic_load(&jit_ss_diag_enabled));
    if (!atomic_load(&jit_ss_diag_enabled))
        return;
    struct ss_diag_snapshot *rows = calloc(SS_DIAG_CONTEXTS, sizeof(*rows));
    if (rows == NULL) {
        proc_printf(buf, "snapshot_allocation_failed 1\n");
        return;
    }
    unsigned visited = 0, count = 0;
    pthread_mutex_lock(&reg_lock);
    struct jit_ctx *c = reg_head;
    for (; c && visited < SS_DIAG_CONTEXTS; c = c->reg_next, visited++)
        if (c->ss_diag)
            ss_diag_copy(c->ss_diag, &rows[count++]);
    bool truncated = c != NULL;
    pthread_mutex_unlock(&reg_lock);
    proc_printf(buf, "version 1\napproximate_counters 1\ncounters_count_attempts 1\nsample_window recent\n"
                "sample_probability_denominator 1024\npath_legend 0=negative 1=exit 2=nomem\n"
                "miss_legend 0=hit 1=cold 2=collision 3=generation\n"
                "sample_ring_size %u\ncontexts_visited %u\ntruncated %d\ncontext_allocation_failed %llu\n",
                SS_DIAG_SAMPLES, visited, truncated, (unsigned long long) atomic_load(&ss_diag_alloc_failed));
    for (unsigned r = 0; r < count; r++) {
        struct ss_diag_snapshot *s = &rows[r];
        if (!s->consistent) {
            proc_printf(buf, "context_busy 1\n");
            continue;
        }
        proc_printf(buf, "context tid=%llu tgid=%llu mmu=%llu reset=%llu sampled=%llu busy_samples=%u",
                    (unsigned long long) s->tid, (unsigned long long) s->tgid,
                    (unsigned long long) s->mmu_id, (unsigned long long) s->reset_seq,
                    (unsigned long long) s->sampled, s->busy_samples);
        for (unsigned i = 0; i < SD_COUNTERS; i++)
            proc_printf(buf, " %s=%llu", names[i], (unsigned long long) s->counters[i]);
        proc_printf(buf, "\n");
        for (unsigned i = 0; i < SS_DIAG_SAMPLES; i++) {
            if (!s->samples[i].serial)
                continue;
            proc_printf(buf, "sample tid=%llu mmu=%llu serial=%llu pc=%#llx insn=%08llx readable=%llu path=%llu miss=%llu\n",
                        (unsigned long long) s->tid, (unsigned long long) s->mmu_id,
                        (unsigned long long) s->samples[i].serial, (unsigned long long) s->samples[i].pc,
                        (unsigned long long) s->samples[i].insn, (unsigned long long) s->samples[i].readable,
                        (unsigned long long) s->samples[i].origin, (unsigned long long) s->samples[i].miss);
        }
    }
    free(rows);
}

struct ss_entry {
    addr_t pc;
    struct fiber_block *block;
    struct ss_entry *next;
    struct ss_entry **hash_prev;      // link pointing here in the PC hash; repaired on resize
    struct ss_entry *page_next;
};

static inline size_t pc_hash(addr_t pc, size_t size) {
    return (size_t) ((pc * 0x9e3779b97f4a7c15ull) >> 32) & (size - 1);
}

static void ss_grow_locked(struct jit_mm *mm) {
    if (mm->ss_count < mm->ss_grow_at)
        return;
    // Back off after failure until the live entry count doubles, rather than
    // attempting an allocation for every subsequently cached instruction.
    mm->ss_grow_at = mm->ss_count <= SIZE_MAX / 2 ? mm->ss_count * 2 : SIZE_MAX;
    size_t size = mm->ss_size;
    do {
        if (size > SIZE_MAX / 2 / sizeof(*mm->ss_hash))
            return;
        size *= 2;
    } while (size <= mm->ss_count);
    struct ss_entry **hash = mem_host_alloc_fails() ? NULL : calloc(size, sizeof(*hash));
    if (hash == NULL)
        return;                     // allocation failure only makes lookup slower
    for (size_t i = 0; i < mm->ss_size; i++) {
        struct ss_entry *e = mm->ss_hash[i];
        while (e) {
            struct ss_entry *next = e->next;
            size_t h = pc_hash(e->pc, size);
            e->next = hash[h];
            e->hash_prev = &hash[h];
            if (e->next)
                e->next->hash_prev = &e->next;
            hash[h] = e;
            e = next;
        }
    }
    // Every PC-hash reader holds mm->lock; the page index and entry/block
    // addresses are unchanged, so no epoch retirement or generation bump is
    // needed for the old bucket array.
    free(mm->ss_hash);
    mm->ss_hash = hash;
    mm->ss_size = size;
    mm->ss_grow_at = size;
}

static void ss_insert_locked(struct jit_mm *mm, struct ss_entry *e) {
    ss_grow_locked(mm);
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

static struct fiber_block *ss_get(struct jit_mm *mm, addr_t pc, struct tlb *tlb, bool keep,
        struct jit_ss_cache_entry *cached, enum ss_result *result) {
    pthread_mutex_lock(&mm->lock);
    size_t h = pc_hash(pc, mm->ss_size);
    for (struct ss_entry *e = mm->ss_hash[h]; e; e = e->next) {
        if (e->pc == pc) {
            struct fiber_block *b = e->block;
            if (cached)
                *cached = (struct jit_ss_cache_entry) {pc, b, atomic_load(&mm->gen)};
            if (result)
                *result = SS_SHARED_HIT;
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
        if (result)
            *result = SS_UNTRACKED_OOM;
        pthread_mutex_unlock(&mm->lock);
        return block;
    }
    e->pc = pc;
    e->block = block;
    if ((keep || mm->ss_count < SS_NOMEM_CACHED) && atomic_load(&mm->gen) == gen) {
        ss_insert_locked(mm, e);
        if (cached)
            *cached = (struct jit_ss_cache_entry) {pc, block, gen};
        if (result)
            *result = SS_FILL;
    } else {
        if (result)
            *result = atomic_load(&mm->gen) != gen ? SS_ONESHOT_GENERATION : SS_ONESHOT_PRESSURE;
        e->next = mm->ss_jetsam;   // used once, freed by reclaim
        mm->ss_jetsam = e;
    }
    mm->stats_fallback++;
    pthread_mutex_unlock(&mm->lock);
    return block;
}

static struct fiber_block *ss_get_diagnosed(struct jit_ctx *ctx, addr_t pc, struct tlb *tlb,
        bool keep, enum ss_origin origin) {
    struct jit_ss_diag *d = ctx->ss_diag;
    struct jit_mm *mm = ctx->mm;
    uint64_t gen = atomic_load_explicit(&mm->gen, memory_order_acquire);
    struct jit_ss_cache_entry *cached = &ctx->ss_lc[pc_hash(pc, JIT_SS_LC_SIZE)];
    enum ss_diag_counter reason;
    if (cached->block == NULL)
        reason = SD_COLD;
    else if (cached->gen != gen) {
        reason = SD_GENERATION;
        if (cached->pc == pc)
            ss_diag_count(d, SD_GENERATION_SAME_PC);
    } else if (cached->pc != pc)
        reason = SD_COLLISION;
    else
        reason = SD_HIT;
    ss_diag_count(d, reason);
    ss_diag_count(d, origin == SS_NEGATIVE ? SD_NEGATIVE : origin == SS_EXIT ? SD_EXIT : SD_NOMEM);
    ss_diag_sample(d, pc, tlb, origin, reason);
    if (reason == SD_HIT)
        return cached->block;
    cached->block = NULL;
    enum ss_result result;
    struct fiber_block *block = ss_get(mm, pc, tlb, keep, cached, &result);
    static const enum ss_diag_counter outcomes[] = {
        SD_SHARED_HIT, SD_FILL, SD_ONESHOT_GENERATION, SD_ONESHOT_PRESSURE, SD_UNTRACKED_OOM
    };
    ss_diag_count(d, outcomes[result]);
    return block;
}

static struct fiber_block *ss_get_cached(struct jit_ctx *ctx, addr_t pc, struct tlb *tlb,
        bool keep, enum ss_origin origin) {
    if (__builtin_expect(ctx->ss_diag != NULL, 0))
        return ss_get_diagnosed(ctx, pc, tlb, keep, origin);
    struct jit_mm *mm = ctx->mm;
    uint64_t gen = atomic_load_explicit(&mm->gen, memory_order_acquire);
    struct jit_ss_cache_entry *cached = &ctx->ss_lc[pc_hash(pc, JIT_SS_LC_SIZE)];
    if (cached->pc == pc && cached->gen == gen && cached->block != NULL)
        return cached->block;
    // Only persistent shared entries may fill this slot. One-shot blocks are
    // retired without a generation bump and must never survive in this cache.
    // The active context's existing epoch protects a hit through execution.
    cached->block = NULL;
    return ss_get(mm, pc, tlb, keep, cached, NULL);
}

static void ctx_switch_mm(struct jit_ctx *ctx, struct jit_mm *mm) {
    if (ctx->mm != mm || ctx->mmu_id != mm->mmu->id) {
        tlb_flush_ctx(ctx);
        ctx->mem_changes = __atomic_load_n(&mm->mmu->changes, __ATOMIC_ACQUIRE);
        ctx->mmu_id = mm->mmu->id;
        ctx->lc_gen = 0;
        memset(ctx->lc, 0, sizeof(ctx->lc));
        memset(ctx->ss_lc, 0, sizeof(ctx->ss_lc));
    }
    ctx->mm = mm;
}
static unsigned checks;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); abort(); } checks++; } while(0)
static void init(struct jit_mm *mm) {
    memset(mm, 0, sizeof(*mm));
    static uint64_t next_id;
    mm->mmu = calloc(1, sizeof(*mm->mmu)); assert(mm->mmu);
    mm->mmu->id = ++next_id;
    mm->ss_size = 256; mm->ss_grow_at = 256; mm->page_hash_size = 65536;
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
    free(mm->mmu);
    pthread_mutex_destroy(&mm->lock);
}
static struct ss_entry *add(struct jit_mm *mm, page_t page, unsigned offset) {
    struct ss_entry *e = calloc(1, sizeof(*e)); assert(e);
    e->pc = (page << 12) + offset;
    e->block = &dummy_block;
    pthread_mutex_lock(&mm->lock);
    ss_insert_locked(mm, e);
    pthread_mutex_unlock(&mm->lock);
    return e;
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
    CHECK(ss_get(&m, p << 12 | 4, NULL, true, NULL, NULL) == &dummy_block);
    CHECK(atomic_load(&m.gen) == 1 && m.ss_count == 0 && retired_count(&m) == 2);
    validate(&m);
    allocation_fails = true;
    CHECK(ss_get(&m, p << 12 | 8, NULL, true, NULL, NULL) == &dummy_block);
    CHECK(m.ss_count == 0 && retired_count(&m) == 2);
    allocation_fails = false;
    for (unsigned i = 0; i < SS_NOMEM_CACHED; i++) add(&m, 100 + i, 0);
    CHECK(ss_get(&m, (p + 2) << 12, NULL, false, NULL, NULL) == &dummy_block);
    CHECK(m.ss_count == SS_NOMEM_CACHED && retired_count(&m) == 3);
    CHECK(ss_get(&m, (p + 2) << 12 | 4, NULL, true, NULL, NULL) == &dummy_block);
    CHECK(m.ss_count == SS_NOMEM_CACHED + 1);
    CHECK(ss_get(&m, (p + 2) << 12 | 4, NULL, true, NULL, NULL) == &dummy_block);
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
static struct jit_mm *bump_on_unlock;
static void bump_generation_after_unlock(void) {
    atomic_fetch_add(&bump_on_unlock->gen, 1);
}
static bool ss_cache_empty(struct jit_ctx *ctx) {
    for (unsigned i = 0; i < JIT_SS_LC_SIZE; i++)
        if (ctx->ss_lc[i].block) return false;
    return true;
}
static void context_cache_tests(void) {
    const addr_t pc = UINT64_C(0x800100004000);
    struct jit_mm m, other; init(&m); init(&other);
    struct jit_ctx ctx = {0}; ctx_switch_mm(&ctx, &m);
    CHECK(ctx.mm == &m && ctx.mmu_id == m.mmu->id && ctx.tlb_flushes == 1);
    struct fiber_block *old = malloc(sizeof(*old)); assert(old);
    add(&m, PAGE(pc), 0)->block = old;
    unsigned slot = pc_hash(pc, JIT_SS_LC_SIZE), locks = mutex_lock_calls;
    CHECK(ss_get_cached(&ctx, pc, NULL, true, SS_EXIT) == old);
    CHECK(mutex_lock_calls == locks + 1 && ctx.ss_lc[slot].block == old);
    locks = mutex_lock_calls;
    for (unsigned i = 0; i < 100000; i++) CHECK(ss_get_cached(&ctx, pc, NULL, true, SS_EXIT) == old);
    CHECK(mutex_lock_calls == locks); // actual production fast hits take no lock

    // After a quiescent epoch, the retired block really is freed while its
    // stale local pointer remains. The new generation must prevent its use.
    jit_invalidate_range(&m, PAGE(pc), PAGE(pc) + 1);
    CHECK(m.ss_count == 0 && retired_count(&m) == 1);
    struct ss_entry *retired = m.ss_jetsam; m.ss_jetsam = NULL;
    free(retired->block); free(retired);
    struct fiber_block replacement;
    generated_block = &replacement;
    CHECK(ss_get_cached(&ctx, pc, NULL, true, SS_EXIT) == &replacement);
    CHECK(ctx.ss_lc[slot].gen == atomic_load(&m.gen));
    generated_block = &dummy_block;

    // Same-slot collisions cannot return another PC's code.
    addr_t collision = pc + 4;
    while (pc_hash(collision, JIT_SS_LC_SIZE) != slot) collision += 4;
    add(&m, PAGE(collision), collision & 4095);
    CHECK(ss_get_cached(&ctx, collision, NULL, true, SS_EXIT) == &dummy_block);
    CHECK(ss_get_cached(&ctx, pc, NULL, true, SS_EXIT) == &replacement);

    // A generation change just after the slow lookup unlocks must not be
    // mistaken for the generation under which that result was obtained.
    memset(ctx.ss_lc, 0, sizeof(ctx.ss_lc));
    uint64_t before_gen = atomic_load(&m.gen);
    bump_on_unlock = &m; after_unlock_hook = bump_generation_after_unlock;
    CHECK(ss_get_cached(&ctx, pc, NULL, true, SS_EXIT) == &replacement);
    CHECK(ctx.ss_lc[slot].gen == before_gen && atomic_load(&m.gen) == before_gen + 1);
    locks = mutex_lock_calls;
    CHECK(ss_get_cached(&ctx, pc, NULL, true, SS_EXIT) == &replacement);
    CHECK(mutex_lock_calls == locks + 1 && ctx.ss_lc[slot].gen == before_gen + 1);

    // The exact production switch helper resets both caches. This also covers
    // address-space pointer reuse (new mmu id) and ctx_activate's trim reset
    // trigger (ctx->mm=NULL), without pretending to emulate host madvise.
    add(&other, PAGE(pc), 0);
    ctx.lc[0] = &replacement; ctx.lc_gen = 123;
    ctx_switch_mm(&ctx, &other);
    CHECK(ss_cache_empty(&ctx) && ctx.lc[0] == NULL && ctx.lc_gen == 0);
    CHECK(ss_get_cached(&ctx, pc, NULL, true, SS_EXIT) == &dummy_block);
    unsigned flushes = ctx.tlb_flushes;
    ctx_switch_mm(&ctx, &other);
    CHECK(ctx.tlb_flushes == flushes && !ss_cache_empty(&ctx));
    other.mmu->id++; other.mmu->changes = 42;
    ctx_switch_mm(&ctx, &other);
    CHECK(ss_cache_empty(&ctx) && ctx.tlb_flushes == flushes + 1 && ctx.mem_changes == 42);
    CHECK(ss_get_cached(&ctx, pc, NULL, true, SS_EXIT) == &dummy_block);
    ctx.mm = NULL; // same trigger set by the actual trimmed-context path
    ctx_switch_mm(&ctx, &other);
    CHECK(ss_cache_empty(&ctx) && ctx.tlb_flushes == flushes + 2);
    destroy(&m); destroy(&other);

    init(&m); memset(&ctx, 0, sizeof(ctx)); ctx_switch_mm(&ctx, &m);
    add(&m, PAGE(pc), 0);
    invalidate_during_translation = &m; invalidation_page = PAGE(pc);
    CHECK(ss_get_cached(&ctx, pc + 4, NULL, true, SS_EXIT) == &dummy_block);
    CHECK(ss_cache_empty(&ctx) && m.ss_count == 0 && retired_count(&m) == 2);
    unsigned generated = translations;
    CHECK(ss_get_cached(&ctx, pc + 4, NULL, true, SS_EXIT) == &dummy_block);
    CHECK(translations == generated + 1 && m.ss_count == 1);
    allocation_fails = true;
    CHECK(ss_get_cached(&ctx, pc + 8, NULL, true, SS_EXIT) == &dummy_block);
    CHECK(ctx.ss_lc[pc_hash(pc + 8, JIT_SS_LC_SIZE)].block == NULL);
    allocation_fails = false;
    generated = translations;
    CHECK(ss_get_cached(&ctx, pc + 8, NULL, true, SS_EXIT) == &dummy_block);
    CHECK(translations == generated + 1);
    destroy(&m);

    init(&m); memset(&ctx, 0, sizeof(ctx)); ctx_switch_mm(&ctx, &m);
    for (unsigned i = 0; i < SS_NOMEM_CACHED; i++) add(&m, 100 + i, 0);
    generated = translations;
    CHECK(ss_get_cached(&ctx, pc, NULL, false, SS_EXIT) == &dummy_block);
    CHECK(ss_cache_empty(&ctx) && retired_count(&m) == 1);
    CHECK(ss_get_cached(&ctx, pc, NULL, false, SS_EXIT) == &dummy_block);
    CHECK(ss_cache_empty(&ctx) && retired_count(&m) == 2 && translations == generated + 2);
    // Persistent results remain reusable even if a later caller says keep=false.
    CHECK(ss_get_cached(&ctx, pc, NULL, true, SS_EXIT) == &dummy_block);
    locks = mutex_lock_calls;
    CHECK(ss_get_cached(&ctx, pc, NULL, false, SS_EXIT) == &dummy_block);
    CHECK(mutex_lock_calls == locks);
    destroy(&m);
}
static void growth_tests(void) {
    enum { N = 32769 };
    const addr_t base = UINT64_C(0x800000000000);
    struct fiber_block *blocks = calloc(N, sizeof(*blocks)); assert(blocks);
    struct jit_mm m; init(&m);
    for (unsigned i = 0; i < N; i++) {
        // High FEX-like code addresses, 64-byte aligned; every PC gets its
        // own result so a stale bucket or a wrong hit cannot pass unnoticed.
        addr_t pc = base + i * 64;
        add(&m, PAGE(pc), pc & 4095)->block = &blocks[i];
        if (i && (i & (i - 1)) == 0) validate(&m);
    }
    CHECK(m.ss_size == 65536 && m.ss_grow_at == 65536);
    CHECK(atomic_load(&m.gen) == 0); // structural resize is not code invalidation
    unsigned before = translations;
    for (unsigned i = 0; i < N; i++)
        CHECK(ss_get(&m, base + i * 64, NULL, true, NULL, NULL) == &blocks[i]);
    CHECK(translations == before);
    // Deletion through the page index uses repaired PC-chain backlinks after
    // eight growths; remaining hits and replacement blocks must stay exact.
    jit_invalidate_range(&m, PAGE(base) + 17, PAGE(base) + 131);
    validate(&m);
    for (unsigned i = 0; i < N; i++) {
        addr_t pc = base + i * 64;
        bool removed = PAGE(pc) >= PAGE(base) + 17 && PAGE(pc) < PAGE(base) + 131;
        CHECK(contains(&m, pc) == !removed);
        if (removed) add(&m, PAGE(pc), pc & 4095)->block = &blocks[i];
        CHECK(ss_get(&m, pc, NULL, true, NULL, NULL) == &blocks[i]);
    }
    validate(&m);
    CHECK(atomic_load(&m.gen) == 1 && m.ss_count == N);
    destroy(&m); free(blocks);

    init(&m);
    allocation_checks = 0; allocation_fails = true;
    for (unsigned i = 0; i < 1024; i++) add(&m, 0x80000 + i, 0);
    CHECK(m.ss_size == 256 && m.ss_count == 1024 && m.ss_grow_at == 1024);
    CHECK(allocation_checks == 2); // attempts at 256 and 512, not each insert
    validate(&m);
    allocation_fails = false;
    add(&m, 0x90000, 0);
    CHECK(m.ss_size == 2048 && m.ss_grow_at == 2048 && allocation_checks == 3);
    validate(&m);
    jit_invalidate_range(&m, 0x80000, 0x80000 + 1024);
    validate(&m);
    CHECK(m.ss_count == 1 && contains(&m, UINT64_C(0x90000) << 12));
    destroy(&m);

    // Overflow guard must return before allocating or indexing a fictitious
    // enormous table; no production allocation of that size is required.
    init(&m);
    size_t original_size = m.ss_size;
    m.ss_count = m.ss_size = SIZE_MAX / sizeof(*m.ss_hash);
    m.ss_grow_at = m.ss_count;
    before = allocation_checks;
    ss_grow_locked(&m);
    CHECK(allocation_checks == before && m.ss_size == SIZE_MAX / sizeof(*m.ss_hash));
    m.ss_count = 0; m.ss_size = original_size;
    destroy(&m);
}
struct reader_args {
    struct jit_mm *mm;
    struct fiber_block *blocks;
    _Atomic bool *start;
};
static void *lookup_reader(void *opaque) {
    struct reader_args *a = opaque;
    while (!atomic_load(a->start)) sched_yield();
    for (unsigned i = 0; i < 100000; i++) {
        unsigned key = (i * 73) % 256;
        assert(ss_get(a->mm, UINT64_C(0x800100000000) + key * 64, NULL, true, NULL, NULL) == &a->blocks[key]);
    }
    return NULL;
}
static void concurrent_growth_test(void) {
    struct jit_mm m; init(&m);
    struct fiber_block blocks[256];
    for (unsigned i = 0; i < 256; i++) {
        addr_t pc = UINT64_C(0x800100000000) + i * 64;
        add(&m, PAGE(pc), pc & 4095)->block = &blocks[i];
    }
    _Atomic bool start = false;
    struct reader_args args = {&m, blocks, &start};
    pthread_t thread; assert(pthread_create(&thread, NULL, lookup_reader, &args) == 0);
    atomic_store(&start, true);
    for (unsigned i = 256; i < 32769; i++) {
        addr_t pc = UINT64_C(0x800100000000) + i * 64;
        add(&m, PAGE(pc), pc & 4095);
    }
    assert(pthread_join(thread, NULL) == 0);
    CHECK(m.ss_size == 65536 && m.ss_count == 32769);
    validate(&m); destroy(&m);
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
    m.ss_grow_at = SIZE_MAX; // preserve the original invalidation control layout
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
// Pre-growth ss_get hit path, with the old fixed 256-bucket table. Miss
// generation is excluded: all timed requests must hit an existing instruction.
static struct fiber_block *old_ss_get_hit(struct jit_mm *mm, addr_t pc) {
    size_t h = pc_hash(pc, mm->ss_size);
    pthread_mutex_lock(&mm->lock);
    for (struct ss_entry *e = mm->ss_hash[h]; e; e = e->next) {
        if (e->pc == pc) {
            struct fiber_block *b = e->block;
            pthread_mutex_unlock(&mm->lock);
            return b;
        }
    }
    abort();
}
static uint64_t hit_visits(struct jit_mm *mm, addr_t pc) {
    uint64_t visits = 0;
    for (struct ss_entry *e = mm->ss_hash[pc_hash(pc, mm->ss_size)]; e; e = e->next) {
        visits++;
        if (e->pc == pc) return visits;
    }
    abort();
}
static void lookup_benchmark(void) {
    enum { N = 65536, ROUNDS = 65536 };
    const unsigned strides[] = {4, 64, 4096, 65536};
    for (unsigned pattern = 0; pattern < sizeof(strides) / sizeof(*strides); pattern++) {
        struct jit_mm old, grown; init(&old); init(&grown);
        old.ss_grow_at = SIZE_MAX; // reproduce the original fixed PC index
        unsigned stride = strides[pattern];
        const addr_t base = UINT64_C(0x800000000000);
        for (unsigned i = 0; i < N; i++) {
            addr_t pc = base + (uint64_t)i * stride;
            add(&old, PAGE(pc), pc & 4095); add(&grown, PAGE(pc), pc & 4095);
        }
        uint64_t old_visits = 0, grown_visits = 0;
        for (unsigned i = 0; i < ROUNDS; i++) {
            addr_t pc = base + (uint64_t)((i * 40503u) % N) * stride;
            old_visits += hit_visits(&old, pc); grown_visits += hit_visits(&grown, pc);
        }
        unsigned before = translations;
        uint64_t begin = clock_gettime_nsec_np(CLOCK_MONOTONIC);
        for (unsigned i = 0; i < ROUNDS; i++) {
            addr_t pc = base + (uint64_t)((i * 40503u) % N) * stride;
            assert(old_ss_get_hit(&old, pc) == &dummy_block);
        }
        uint64_t old_ns = clock_gettime_nsec_np(CLOCK_MONOTONIC) - begin;
        begin = clock_gettime_nsec_np(CLOCK_MONOTONIC);
        for (unsigned i = 0; i < ROUNDS; i++) {
            addr_t pc = base + (uint64_t)((i * 40503u) % N) * stride;
            assert(ss_get(&grown, pc, NULL, true, NULL, NULL) == &dummy_block);
        }
        uint64_t grown_ns = clock_gettime_nsec_np(CLOCK_MONOTONIC) - begin;
        assert(translations == before && old.ss_size == 256 && grown.ss_size == N);
        // The existing shared hash has some clustering for consecutive 4-byte
        // PCs (up to ten nodes here), but growth still bounds this workload's
        // average well below the original ~128-node successful lookup.
        assert(grown_visits < ROUNDS * 8 && old_visits > ROUNDS * 100);
        printf("LOOKUP_BENCH entries=%u hits=%u stride=%u old_buckets=%zu grown_buckets=%zu "
               "old_visits=%" PRIu64 " grown_visits=%" PRIu64 " old_ns=%" PRIu64 " grown_ns=%" PRIu64 "\n",
               N, ROUNDS, stride, old.ss_size, grown.ss_size, old_visits, grown_visits, old_ns, grown_ns);
        validate(&old); validate(&grown); destroy(&old); destroy(&grown);
    }
}
struct cache_thread_args {
    struct jit_mm *mm;
    _Atomic unsigned *ready;
    _Atomic bool *start;
    unsigned rounds, index, lock_calls;
    bool local;
};
static void *cache_hit_worker(void *opaque) {
    struct cache_thread_args *a = opaque;
    struct jit_ctx ctx = {0}; ctx_switch_mm(&ctx, a->mm);
    addr_t pc = UINT64_C(0x800300000000) + a->index * 64;
    assert(ss_get_cached(&ctx, pc, NULL, true, SS_EXIT) == &dummy_block);
    atomic_fetch_add(a->ready, 1);
    while (!atomic_load(a->start)) sched_yield();
    unsigned before = mutex_lock_calls;
    for (unsigned i = 0; i < a->rounds; i++) {
        struct fiber_block *block = a->local ? ss_get_cached(&ctx, pc, NULL, true, SS_EXIT) :
            ss_get(a->mm, pc, NULL, true, NULL, NULL);
        assert(block == &dummy_block);
    }
    a->lock_calls = mutex_lock_calls - before;
    return NULL;
}
static void context_cache_benchmark(void) {
    enum { THREADS = 8, ROUNDS = 50000 };
    struct jit_mm m; init(&m);
    for (unsigned i = 0; i < THREADS; i++) {
        addr_t pc = UINT64_C(0x800300000000) + i * 64;
        add(&m, PAGE(pc), pc & 4095);
    }
    for (unsigned local = 0; local < 2; local++) {
        _Atomic unsigned ready = 0; _Atomic bool start = false;
        pthread_t threads[THREADS]; struct cache_thread_args args[THREADS];
        for (unsigned i = 0; i < THREADS; i++) {
            args[i] = (struct cache_thread_args) {&m, &ready, &start, ROUNDS, i, 0, local};
            assert(pthread_create(&threads[i], NULL, cache_hit_worker, &args[i]) == 0);
        }
        while (atomic_load(&ready) != THREADS) sched_yield();
        uint64_t begin = clock_gettime_nsec_np(CLOCK_MONOTONIC);
        atomic_store(&start, true);
        unsigned locks = 0;
        for (unsigned i = 0; i < THREADS; i++) {
            assert(pthread_join(threads[i], NULL) == 0); locks += args[i].lock_calls;
        }
        uint64_t elapsed = clock_gettime_nsec_np(CLOCK_MONOTONIC) - begin;
        assert(locks == (local ? 0 : THREADS * ROUNDS));
        printf("CONTEXT_CACHE_BENCH threads=%u hits=%u local_cache=%u mutex_locks=%u elapsed_ns=%" PRIu64 "\n",
               THREADS, THREADS * ROUNDS, local, locks, elapsed);
    }
    validate(&m); destroy(&m);
}
static void diagnostic_tests(void) {
    struct jit_mm m; init(&m);
    struct jit_ss_diag d = {0};
    struct jit_ctx ctx = {.ss_diag = &d}; ctx_switch_mm(&ctx, &m);
    struct tlb tlb = {.readable = true, .insn = 0xd51b4400};
    addr_t pc = UINT64_C(0x800300001000);
    ss_diag_begin(&d, m.mmu->id, 42, 40);
    CHECK(ss_get_cached(&ctx, pc, &tlb, true, SS_NEGATIVE) == &dummy_block);
    CHECK(atomic_load(&d.counters[SD_COLD]) == 1 && atomic_load(&d.counters[SD_FILL]) == 1);
    for (unsigned i = 0; i < 100000; i++)
        CHECK(ss_get_cached(&ctx, pc, &tlb, true, i % 3) == &dummy_block);
    CHECK(atomic_load(&d.counters[SD_HIT]) == 100000);
    CHECK(atomic_load(&d.counters[SD_NEGATIVE]) == 33335);
    CHECK(atomic_load(&d.counters[SD_EXIT]) == 33333);
    CHECK(atomic_load(&d.counters[SD_NOMEM]) == 33333);
    struct ss_diag_snapshot snap;
    ss_diag_copy(&d, &snap);
    CHECK(snap.consistent && snap.tid == 42 && snap.tgid == 40 && snap.sampled > 64);
    unsigned paths = 0;
    for (unsigned i = 0; i < SS_DIAG_SAMPLES; i++) {
        CHECK(snap.samples[i].serial > 0 && snap.samples[i].readable);
        CHECK(snap.samples[i].pc == pc && snap.samples[i].insn == 0xd51b4400);
        CHECK(snap.samples[i].miss == SD_HIT);
        paths |= 1u << snap.samples[i].origin;
    }
    CHECK(paths == 7); // cached hits sampled on every fallback source path
    addr_t collision = pc + 4;
    while (pc_hash(collision, JIT_SS_LC_SIZE) != pc_hash(pc, JIT_SS_LC_SIZE)) collision += 4;
    CHECK(ss_get_cached(&ctx, collision, &tlb, true, SS_EXIT) == &dummy_block);
    CHECK(ss_get_cached(&ctx, pc, &tlb, true, SS_EXIT) == &dummy_block);
    CHECK(atomic_load(&d.counters[SD_COLLISION]) == 2);
    CHECK(atomic_load(&d.counters[SD_SHARED_HIT]) == 1);
    atomic_fetch_add(&m.gen, 1);
    CHECK(ss_get_cached(&ctx, pc, &tlb, true, SS_EXIT) == &dummy_block);
    CHECK(atomic_load(&d.counters[SD_GENERATION]) == 1);
    CHECK(atomic_load(&d.counters[SD_GENERATION_SAME_PC]) == 1);
    allocation_fails = true;
    CHECK(ss_get_cached(&ctx, pc + 8, &tlb, true, SS_EXIT) == &dummy_block);
    allocation_fails = false;
    CHECK(atomic_load(&d.counters[SD_UNTRACKED_OOM]) == 1);
    invalidation_page = PAGE(pc); invalidate_during_translation = &m;
    CHECK(ss_get_cached(&ctx, pc + 12, &tlb, true, SS_EXIT) == &dummy_block);
    CHECK(atomic_load(&d.counters[SD_ONESHOT_GENERATION]) == 1);
    for (unsigned i = 0; i < SS_NOMEM_CACHED; i++) add(&m, 100 + i, 0);
    CHECK(ss_get_cached(&ctx, pc + 16, &tlb, false, SS_NOMEM) == &dummy_block);
    CHECK(atomic_load(&d.counters[SD_ONESHOT_PRESSURE]) == 1);
    validate(&m); destroy(&m);

    ss_diag_begin(&d, UINT64_C(123456789), 99, 98);
    ss_diag_copy(&d, &snap);
    CHECK(snap.consistent && snap.mmu_id == UINT64_C(123456789) && snap.tid == 99);
    CHECK(snap.sampled == 0 && snap.counters[SD_HIT] == 0);
    struct tlb unreadable = {.segfault_addr = 0x1234};
    for (unsigned i = 0; i < 100000; i++) ss_diag_sample(&d, pc, &unreadable, SS_EXIT, SD_COLD);
    CHECK(unreadable.segfault_addr == 0x1234);
    ss_diag_copy(&d, &snap);
    CHECK(snap.sampled > 64);
    for (unsigned i = 0; i < SS_DIAG_SAMPLES; i++)
        CHECK(snap.samples[i].serial && !snap.samples[i].readable && snap.samples[i].insn == 0);
    atomic_fetch_add(&d.reset_seq, 1);
    ss_diag_copy(&d, &snap); CHECK(!snap.consistent); // bounded retry during identity reset
    atomic_fetch_add(&d.reset_seq, 1);

    struct proc_data *buf = calloc(1, sizeof(*buf)); CHECK(buf != NULL);
    atomic_store(&jit_ss_diag_enabled, false);
    jit_ss_diag_show(buf); CHECK(strstr(buf->text, "enabled 0") && !strstr(buf->text, "context tid="));
    ss_diag_begin(&d, 987654, 99, 98); // no ring output in the traversal-bound test
    struct jit_ctx *contexts = calloc(SS_DIAG_CONTEXTS + 1, sizeof(*contexts)); CHECK(contexts != NULL);
    for (unsigned i = 0; i <= SS_DIAG_CONTEXTS; i++) {
        contexts[i].ss_diag = &d;
        contexts[i].reg_next = i < SS_DIAG_CONTEXTS ? &contexts[i+1] : NULL;
    }
    reg_head = contexts; atomic_store(&jit_ss_diag_enabled, true);
    memset(buf, 0, sizeof(*buf)); jit_ss_diag_show(buf);
    CHECK(strstr(buf->text, "contexts_visited 128") && strstr(buf->text, "truncated 1"));
    reg_head = NULL; free(contexts); free(buf);
}
struct diag_thread_args { struct jit_ss_diag *d; _Atomic bool done; };
static void *diag_writer(void *p) {
    struct diag_thread_args *a = p;
    struct tlb tlb = {.readable = true};
    for (unsigned i = 1; i <= 500000; i++) {
        if ((i & 8191) == 1) ss_diag_begin(a->d, i, i, i);
        tlb.insn = i;
        ss_diag_count(a->d, SD_HIT);
        ss_diag_sample(a->d, (addr_t)i << 2, &tlb, SS_EXIT, SD_HIT);
    }
    atomic_store(&a->done, true); return NULL;
}
static void diagnostic_concurrent_test(void) {
    struct jit_ss_diag d = {0}; struct diag_thread_args a = {.d = &d};
    pthread_t t; CHECK(pthread_create(&t, NULL, diag_writer, &a) == 0);
    unsigned copied = 0;
    do {
        struct ss_diag_snapshot s; ss_diag_copy(&d, &s);
        if (!s.consistent) continue;
        copied++;
        CHECK(s.mmu_id == s.tid && s.tid == s.tgid);
        for (unsigned i = 0; i < SS_DIAG_SAMPLES; i++) {
            if (!s.samples[i].serial) continue;
            CHECK(s.samples[i].readable && (s.samples[i].pc >> 2) == s.samples[i].insn);
            CHECK(s.samples[i].origin == SS_EXIT && s.samples[i].miss == SD_HIT);
        }
    } while (!atomic_load(&a.done));
    CHECK(pthread_join(t, NULL) == 0 && copied > 0);
}
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--benchmark") == 0) { benchmark(); return 0; }
    if (argc == 2 && strcmp(argv[1], "--lookup-benchmark") == 0) { lookup_benchmark(); return 0; }
    if (argc == 2 && strcmp(argv[1], "--context-benchmark") == 0) { context_cache_benchmark(); return 0; }
    focused_tests(); context_cache_tests(); growth_tests(); concurrent_growth_test(); randomized_tests();
    diagnostic_tests(); diagnostic_concurrent_test();
    context_cache_benchmark(); // exercise independent thread caches under ASan/UBSan too
    printf("PASS actual production cache functions: %u assertions; index integrity after every range; sanitizer flags recorded with invocation\n", checks);
    return 0;
}
