# Independent per-context SS-cache review

Review result: no blocking correctness issue. This review did not change source.
The adjacent JSON pins the exact three reviewed files and existing test evidence.

The new 256-entry cache belongs to a single jit_ctx. A hit compares the full PC,
non-null block and current acquired mm generation. Slow lookup fills the slot
under mm->lock using the generation belonging to that result. An invalidation
immediately after unlock therefore leaves an old-generation slot; the next
lookup misses. One-shot, allocation-failure and generation-mismatch results
never fill the slot. keep=false can reuse an already persistent shared entry,
matching the previous shared-cache behavior.

Existing page/all invalidation bumps mm->gen when removing SS entries. The
existing active context epoch protects a block through execution, and the next
dispatch epoch may permit reclamation; stale cached pointers are not dereferenced
on generation mismatch. The patch preserves the existing concurrent guest
self-modification/IC IVAU contract, rather than adding stronger cross-thread
instruction synchronization. No new lock or ownership path is introduced.

The extracted ctx_switch_mm helper preserves prior TLB/compiled-cache reset
behavior and clears the SS cache on pointer or mmu-id change. The trimmed
context path sets ctx->mm=NULL before that helper. New context mapping is
zero-filled. Added storage follows the fixed assembly header/TLB offsets;
sizeof-based allocation/trim and cache-layout salt account for the new size.
The additional cache payload is 6144 bytes per context (256×24 bytes), subject
to host-page allocation rounding.

The actual-function harness has 438495 ASan/UBSan assertions. New checks include
100000 fast hits without a shared lock, a stale pointer after actual free,
PC collisions, generation change immediately after unlock, pointer/id/trim
reset triggers, one-shot/OOM rejection, and independent threaded cache hits.
The eight-thread 400000-hit comparison records 400000 shared mutex calls versus
zero after per-context warmup. This is a local cache workload, not a predicted
application speedup or physical frame-rate result. Real SMC/Mono controls on
the rebuilt CLI remain the next semantic gate.
