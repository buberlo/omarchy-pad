# Per-context fallback instruction cache

Commit f93f1449 adds a 256-entry persistent single-step cache to each JIT
context. It follows the separate physical build4449 presentation-stage profile
that identified shared ss_get mutex waiting and wakeups across game threads.
That profile belongs to build4449, not to this new source or build4450.

Each entry stores the full guest PC, block pointer and mm generation. The slow
path fills only persistent shared entries while holding the existing mm lock.
One-shot, allocation-failure and generation-mismatch results remain excluded.
Hits retain the existing epoch lifetime and invalidation-generation contract.
Address-space pointer/id changes and reactivation after idle trim clear the
cache. The fixed assembly header/TLB offsets remain unchanged; sizeof-based
context allocation and persistent cache layout salt account for the added
6 KiB payload. Page indexing and shared-table growth remain intact.

Independent source review found no blocking correctness issue. The actual-
function host harness passes 438,495 assertions with ASan/UBSan, including
an actually freed stale block rejected by generation, PC collisions, a
generation change immediately after unlocking, mm/id/trim resets, one-shot
exclusion and allocator failure. Existing invalidation/growth controls pass.
Eight host threads perform 400,000 persistent hits: the shared path takes
400,000 mutex locks and the warmed local path takes zero. This proves the
mechanism on the synthetic workload, not the actual game cache hit rate,
device performance or FPS.

The rebuilt local CLI hash is
b47c299a4da7445ee0aa2ba8dd5accd6ac682d2425a6af125e682988a348ef35.
Its recorded JIT source hashes match f93f1449. Unchanged real ARM64 ordinary
SMC and actual MSR-FPCR fallback SMC both pass in normal JIT and forced tier2,
with 200 rewrites each. Exact default-FEX Mono noSDL and full embedded Unity
SDL0x3221 with local dummy video return nonnull domains and exit0 (2.482 and
3.101 seconds respectively). These timings are local functional observations
under shared host load, not an iPad speed comparison.

source/ contains exact committed production/test and unchanged SMC control
sources; source-change.patch is the committed change. host/ preserves the
earlier analysis, independent review and extracted harness. Its precommit
provenance records the earlier HEAD plus then-pending documentation drafts;
it does not change the exact source pin. The finalized prose was committed
separately as 0afc2925 and its exact blobs and patch are now included under
documentation/. That later documentation commit is not the app source pin.
runtime-controls/ contains small own-process
logs and narrowed Mono excerpts. Large syscall traces retain size/hash records
but remain local, as do CLI binaries and proprietary Unity/Mono/game bytes.

Build/sign/install/physical validation of build4450 are separate parent-owned
gates. Existing build4449 screenshots and cadence cannot be attributed to this
new cache. No game rendering, sound, playability or physical FPS is inferred.
