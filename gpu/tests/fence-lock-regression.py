#!/usr/bin/env python3
"""Deterministic host test of the production GPU fence completion/readiness code.

Extracts the two production functions unchanged, with a small poll-set model:
poll_wait holds the set lock while checking readiness; poll_wakeup needs that
lock. No GPU, guest, or device is needed. --check-legacy-lock also demonstrates
that the previous locked readiness implementation deadlocks this schedule.
"""
import argparse
from pathlib import Path
import re
import signal
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check-legacy-lock', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / 'fs/dev_virtgpu.c').read_text()


def definition(prefix):
    start = source.index(prefix)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    if source[end:end + 1] == ';':
        end += 1
    return source[start:end]


header = r'''
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#include "util/list.h"
#define POLL_READ 1
struct fd { void *data; };
'''
constants = '\n'.join(re.findall(r'^#define MAX_(?:RINGS|CTXS) .+$', source, re.M))
structures = definition('struct vgpu_ctx {') + '\n' + definition('struct vgpu_fence {')
support = r'''
static pthread_mutex_t ctx_table_lock = PTHREAD_MUTEX_INITIALIZER;
static struct vgpu_ctx *ctx_table[MAX_CTXS];
static pthread_mutex_t poller_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned poller_pending;
static pthread_mutex_t set_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t stage_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t stage_cond = PTHREAD_COND_INITIALIZER;
static int callback_entered_wakeup;
static unsigned wakeups;
static void ctx_release(struct vgpu_ctx *ctx) { ctx->refcount--; }
static void poll_wakeup(struct fd *fd, int events) {
    (void)fd;
    assert(events == POLL_READ);
    /* Completion still owns fence_lock; let the reader collide here. */
    pthread_mutex_lock(&stage_lock);
    callback_entered_wakeup = 1;
    pthread_cond_signal(&stage_cond);
    pthread_mutex_unlock(&stage_lock);
    pthread_mutex_lock(&set_lock);
    wakeups++;
    pthread_mutex_unlock(&set_lock);
}
'''
test = r'''
static void *complete(void *unused) {
    (void)unused;
    write_context_fence(NULL, 1, 0, 7);
    return NULL;
}
int main(void) {
    alarm(3);
    struct vgpu_ctx ctx = {0};
    ctx.id = 1;
    atomic_init(&ctx.refcount, 1);
    for (unsigned i = 0; i < MAX_RINGS; i++) atomic_init(&ctx.signaled[i], 0);
    ctx.submitted[0] = 7;
    pthread_mutex_init(&ctx.fence_lock, NULL);
    list_init(&ctx.fences);
    struct fd fd = {0};
    struct vgpu_fence fence = {.ctx=&ctx, .ring=0, .id=7, .fd=&fd};
    fd.data = &fence;
    list_add(&ctx.fences, &fence.ctx_link);
    ctx_table[1] = &ctx;
    poller_pending = 1;
    assert(fence_poll(&fd) == 0);
    pthread_mutex_lock(&set_lock);
    pthread_t worker;
    assert(pthread_create(&worker, NULL, complete, NULL) == 0);
    pthread_mutex_lock(&stage_lock);
    while (!callback_entered_wakeup) pthread_cond_wait(&stage_cond, &stage_lock);
    pthread_mutex_unlock(&stage_lock);
    /* Real poll_wait invokes fence_poll while holding its set lock. */
    assert(fence_poll(&fd) == POLL_READ);
    pthread_mutex_unlock(&set_lock);
    assert(pthread_join(worker, NULL) == 0);
    assert(wakeups == 1 && poller_pending == 0 && ctx.refcount == 1);
    /* An older completion must not regress readiness or decrement twice. */
    write_context_fence(NULL, 1, 0, 6);
    assert(fence_poll(&fd) == POLL_READ && poller_pending == 0 && wakeups == 1);
    alarm(0);
    puts("PASS production GPU fence collision: readiness and completion both finish");
    return 0;
}
'''
legacy = r'''
static int fence_poll(struct fd *fd) {
    struct vgpu_fence *fence = fd->data;
    pthread_mutex_lock(&fence->ctx->fence_lock);
    bool signaled = fence->ctx->signaled[fence->ring] >= fence->id;
    pthread_mutex_unlock(&fence->ctx->fence_lock);
    return signaled ? POLL_READ : 0;
}
'''
with tempfile.TemporaryDirectory(prefix='linpad-fence-regression-') as tmp:
    for old in ([False, True] if args.check_legacy_lock else [False]):
        path = Path(tmp) / ('legacy.c' if old else 'current.c')
        path.write_text(header + constants + '\n' + structures + support +
                        definition('static void write_context_fence(') + '\n' +
                        (legacy if old else definition('static int fence_poll(')) + test)
        binary = path.with_suffix('')
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-pthread',
                        '-I', str(root), str(path), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], timeout=5)
        if old:
            assert result.returncode == -signal.SIGALRM, result.returncode
            print('PASS negative control: previous lock inversion deadlocks the same schedule')
        else:
            result.check_returncode()
