# Independent SS diagnostic review — PASS

Scope: the opt-in SS-cache diagnostic in jit/jit.c, jit/jit_internal.h,
jit/jit.h and the read-only fs/proc/ish.c shim. No device actions, runtime
configuration changes or source commits are part of this review.

The default path keeps the existing cache-generation and epoch contract.
The diagnostic pointer occupies existing context-header padding; the fixed
TLB offset assertion remains. Diagnostic allocations are separate from the
madvise-able context payload. Allocation failure leaves the normal path
available and records an explicit diagnostic allocation-failure count.

Snapshot lifetime is protected by reg_lock only while copying live context
records. The allocation happens before taking that lock; formatting happens
after releasing it. Deregistration under the same lock precedes context and
diagnostic reclamation. No context or mm pointer survives into formatting.
The traversal visits at most 128 contexts, identity copying retries at most
twice, and each context has 64 sample slots. Truncation and busy states are
reported. No signal-handler changes or new mm-lock ordering are introduced.

All fields read concurrently by a snapshot are atomic. The address-space
identity uses a reset sequence, and sample fields use sequentially consistent
atomic serial publication. A sample is accepted only if its nonzero serial
is unchanged. This avoids accepting mixed ring publications; the surrounding
identity sequence rejects samples spanning an address-space reset. Counters
remain approximate across fields and are not a transactionally consistent
instantaneous snapshot.

Both direct negative-block dispatch and native JR_FALLBACK use the same
instrumented lookup. The no-code-memory path is separately labeled. Each
fallback attempt is counted, including retry attempts. Sampling covers both
cache hits and misses. Generation misses take precedence over collisions;
the same-PC generation counter is an overlapping subset, not an additional
miss. Shared-hit, persistent-fill, one-shot-generation, one-shot-pressure
and untracked-allocation outcomes are distinguished.

The sampling ring is the most recent 64 pseudorandom samples at approximately
1/1024 of fallback attempts. It is not a cumulative whole-run opcode histogram.
Sampling and atomic counters have observer cost; diagnostic-build timings are
not an uninstrumented performance result. Live contexts only are retained;
exited worker totals cannot be inferred from a later snapshot.

The exact proc shim compiles with current CLI include flags in both JIT-enabled
and JIT-disabled configurations (proc-shim-syntax.json). The non-JIT branch
prints supported 0, and no write callback is registered. The linked CLI build and real disabled/enabled proc probes pass. The physical
device build/diagnostic run remains a separate pending gate.

The final sampled instruction read saves and restores tlb->segfault_addr,
including when the read fails. Tests cover unreadable instructions, sampling
on every fallback source path, exact miss/fill outcomes, 128-context truncation,
identity resets and simultaneous ring publication/snapshots. Actual-function
ASan/UBSan reports 638893 assertions; the separate ThreadSanitizer run reports
569447 without warnings. These counts vary with concurrent scheduling. The
TSan binary links libclang_rt.tsan_osx_dynamic.dylib; its captured output retains
an old ASan label, which the source owner is correcting in test documentation.
This review does not mistake that label for the active sanitizer.

No production source blocker remains. The diagnostic is observational and
default-off; its counters and sampled reads add cost when enabled, so it must
not be treated as an uninstrumented gameplay performance measurement.
