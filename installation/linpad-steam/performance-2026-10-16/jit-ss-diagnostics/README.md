# Bounded fallback-cache diagnosis

Source commit `45f3e230` adds opt-in diagnosis for the residual fallback-cache
contention measured in build 4450. It changes neither the cache policy nor
native instruction support. The later loading profile still attributed 33.26%
of weighted CPU samples to mutex wait/wake under `ss_get` and 13.26% to CPU-state
copies under `gadget_step`; those samples alone could not explain cache misses.

Launch the **host LinPad app** with `ISH_JIT_SS_DIAG=1`, then read guest
`/proc/ish/jit-ss`. Setting this variable only inside the Linux shell does not
enable the already-running host. `ISH_JIT_STATS` is not required. The default
allocates no diagnostic storage and the normal cache-hit path takes only the
disabled diagnostic branch. Non-JIT builds expose `supported 0`; JIT builds with
diagnosis disabled expose `supported 1` and `enabled 0`.

Each context reports cumulative fallback-attempt counts for hits, cold slots,
same-generation PC collisions, stale generations and the same-PC stale subset.
Generation mismatch takes precedence over PC collision. Miss outcomes identify
shared-cache hits, persistent insertions, generation-rejected one-shot entries,
one-shot entries caused by the existing no-code-cache budget, and untracked
allocation failures. Retry attempts count separately. Counter fields are an
approximate cross-field snapshot.

The most recent 64 pseudorandomly selected attempts per context (approximately
1/1024 probability per attempt) retain exact guest PC, instruction, readable
flag, source path and cache result. Hits are sampled too. Path 0 means direct
negative-block fallback, 1 means a native block's `JR_FALLBACK` exit, and 2 means
fallback after transient code-memory exhaustion. Miss 0 is a hit, 1 cold,
2 collision and 3 generation mismatch. This ring describes a recent window,
not a cumulative opcode histogram. Use TID, MMU identity, reset sequence and
sample serial to distinguish identities and avoid counting duplicate samples
from successive snapshots. An unreadable instruction remains explicitly invalid.

The normal proc reader copies at most 128 registered contexts under the context
registry lock, using atomic reads and at most two identity attempts, then
formats after unlocking. It never takes the shared JIT-memory lock or changes
a signal handler. Separate allocations survive context-payload trimming and
are freed only after deregistration. Output includes truncation, allocation
failures and busy observations. Treat truncated output as incomplete coverage.
Optional instruction reads preserve existing TLB fault metadata.

## Verified locally

- The test harness extracts the actual production cache, diagnostic and proc
  snapshot functions. ASan/UBSan passed 632,999 assertions; a separate
  ThreadSanitizer run passed 576,019 with no race report. Totals vary with the
  concurrent reader's scheduling. Tests cover every miss/outcome class, both
  identity reset and concurrent sample publication, unreadable opcodes with
  fault-metadata preservation, and the 128-context traversal limit.
- The linked macOS-hosted LinPad CLI compiled successfully. The proc shim also
  passed separate syntax checks with JIT enabled and disabled.
- The real AArch64 guest probe ran 100,000 direct-negative MSR FPCR fallbacks and
  100,000 native-exit MSR FPCR fallbacks. Its enabled snapshot reported 199,998
  hits, 2 cold fills and 192 sampled attempts with exact readable FPCR opcodes;
  the enabled and disabled controls both exited zero. Probe assertions are
  bounded to its own context and sample lines.

`validation-provenance.json` records exact build/run commands and binary hashes.
The served probe is `jit-ss-diag-probe`, SHA256
`dce66d840d8c9863b81030a11fb1c1c43a025530547dca683fc9f5dae0c5b9d9`.
After installing it on a diagnostic-enabled guest, run
`/tmp/jit-ss-diag-probe on`; the final line must be
`SS_DIAG_PROC PASS enabled=1`. Without the host flag, omit `on`.

The source patch and blobs are pinned to the committed source. Raw local
control logs remain outside this archive; their hashes accompany concise
excerpts. No proprietary game/runtime libraries are included. Physical build
4451 execution and actual-game miss classification are pending. Diagnostic
timing is not a production-performance or gameplay qualification.
