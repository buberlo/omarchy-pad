# Physical build 4451: fallback diagnosis

Analysis only, captured 2026-10-16. The existing app and game configuration were not modified by this audit. Build 4451 contains diagnostic source commit `45f3e230cac85e946913279094f5e5afbb11c21d`; the opt-in host environment was `ISH_JIT_SS_DIAG=1`.

The two game-PID-348 snapshots at 13:46:02 and 13:46:12 UTC pass the collector's main-thread continuity, identity, and completeness gates. No snapshot or maps truncation is reported. The original report SHA-256 is `3b6cc3877dea40034049b0072115f9bbfcc1e71997b4fd8b241661118e582d0a`.

The main thread recorded 140,699,252 fallback attempts in the matched interval: 119,934,653 context-cache hits (85.24%), 20,282,626 same-generation collisions (14.42%), and 481,973 generation misses (0.34%). Cold misses and allocation/pressure fallback counts were zero. Of the misses, 97.68% were collisions and 99.83% found an existing shared entry. This explains residual shared-cache traffic; it does not measure application FPS.

All 1,223 retained sample observations, including all 128 main-thread samples, match the LRCPC2 `LDAPUR/STLUR` family: `(insn & 0x3f200c00) == 0x19000000`. None matches `MSR FPCR`. These are bounded recent rings, and worker records can repeat between snapshots; 1,223 is neither an independent-random-sample count nor a whole-interval frequency histogram.

Exact instruction words were assembled with `.inst` and decoded with Apple LLVM. Examples are `d9400294 = ldapur x20, [x20]`, `99400174 = ldapur w20, [x11]`, `59000284 = stlurh w4, [x20]`, and `990cc0c4 = stlur w4, [x6, #0xcc]`. This agrees with LinPad's existing gadget decoder and FEX's memory-operation emitter; no byte reversal was needed. The native translator currently supports `LDAPR` and `STLR`, but lacks the sampled unscaled-immediate family.

Every sampled PC belongs to an anonymous executable `rwxp` mapping. Main-thread examples are within `0x800a2b0cc000–0x800a2f0cb000`, at offsets `0x73f794` and `0x2e472a4`. This is consistent with generated code, but does not identify an original x86 Unity function. The maps contain no file name for these regions.

Worker interval differences are deliberately excluded: matching TID/MMU/reset fields alone cannot prove that an exited worker context was not replaced. Their single-snapshot cumulative values remain useful: some workers are collision-heavy, one is generation-heavy, and another is above 99% context hits. No worker names are inferred from TIDs used by earlier game runs.

The next controlled experiment is the existing FEX option `FEX_HOSTFEATURES=disablelrcpc2`, which the separate FEX source audit confirms disables `SupportsTSOImm9` while retaining RCpc loads and release stores. The device owner will compare that configuration on the same signed build. Its outcome was pending when this baseline analysis was written.

`startup-summary.json` is the compact, curated analysis; `startup-analysis.json` is the full local parser result and should not be duplicated into the release archive. `analyze-report.py`, the disassembly, and `provenance.json` permit reproduction. `LRCPC2-DESIGN.md` is a source-based proposal only.
