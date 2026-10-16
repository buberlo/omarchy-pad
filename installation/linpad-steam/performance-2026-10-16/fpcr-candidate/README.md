# FPCR translation candidate — not applied

This is a 12-line candidate for the ARM64 `MSR FPCR, Xt` decoder in `jit/translate.c`. It is outside production, has not been included in a LinPad build, and has not run on the iPad. Build 4451 remains a diagnostic build. Activation depends on its sampled opcodes showing that FPCR writes materially contribute to the single-step workload.

The candidate patch SHA256 is `6a39ab62ff0ee5078e0340d889146cf0e87bbb4225a5ea4aa9629b2ae5bcbee1`. Exact source hashes are in `source-provenance.json`. The source commit at preparation was `45f3e230cac85e946913279094f5e5afbb11c21d`; the inspected `jit.c` and context header also contained the sibling's uncommitted 4451 diagnostic work, captured by file hash. No production file was modified by this preparation.

## Preserved semantics

The current decoder returns fallback for `(insn & 0xffffffe0) == 0xd51b4400`. The gadget stores the source's low 32 bits in `cpu.fpcr`, masks the value with `0x07c80000`, then writes host FPCR. `gadget_step` subsequently derives `guest_fpcr_masked` from the shadow. A later translated-code entry loads that cached value into host FPCR.

The candidate performs the same three state updates inline:

1. Store the source's low 32 bits in `cpu.fpcr`.
2. Compute and store `guest_fpcr_masked = source & 0x07c80000`.
3. Write that masked value to host FPCR.

The mask allows AHP, DN, FZ, rounding mode and FZ16, exactly as the existing gadget and JIT entry do. Exception-enable bits and unexposed FPCR extensions stay excluded from host execution. The mask is intentionally not widened. The full shadow remains available to the gadget engine; it must not be replaced with only the masked hardware value.

`read_greg(..., false)` handles mapped registers, spilled x11–x15/x18, and x31 as XZR. Host x12/x13 are scratch registers and never hold mapped guest registers. The sequence writes no guest GPR, SP, SIMD register, NZCV or FPSR. `AND` is the non-flag-setting form. There are five emitted host instructions for mapped/XZR sources and six for spilled sources.

The existing JIT entry saves host FPCR and applies the cached guest value; exit restores saved host FPCR. The candidate updates the cache because neither `emit_save` nor `emit_restore` reads/writes FPCR. Failing to update it would restore the previous rounding mode after an ordinary exit/reentry. These trampoline operations are unchanged.

FPSR remains a separate state value: the existing JIT save/restore reads and writes it, and the candidate does not. The native test initializes valid cumulative-exception/QC bits and verifies they survive every candidate sequence.

One existing read distinction is deliberately preserved: fast `MRS FPCR` currently reads host hardware, hence the masked value, while gadget `MRS FPCR` reads the full shadow. This candidate neither introduces nor resolves that difference. Tests must not assume unadvertised bits written to the shadow are returned by the fast read. FEX's inspected rounding operations read FPCR and alter the supported rounding/FZ bits; they do not require widening the host mask.

No C helper, allocation, external memory access, new signal handler, or lock is introduced. Context accesses are within the already allocated JIT context. The stores and register operation are idempotent if the existing fault machinery replays the instruction. Full interrupted-JIT/fallback testing is still required before deployment; the standalone harness does not claim to cover signal delivery between emitted words.

## FEX source relevance

The actual Linux FEX revision is `5285020de0b30dbdae7b44d0d83541af2d13d002`. `FEXCore/Source/Interface/Core/JIT/MiscOps.cpp` emits FPCR writes in `SetRoundingMode`, `PushRoundingMode`, and `PopRoundingMode`. These implement x86 rounding/FTZ semantics, so removing them is not a valid shortcut. `SetRoundingMode`'s extra FIZ handling requires AFP; LinPad's sanitized MMFR1 currently reports zero, so AFP is unavailable through that feature path. Feature overrides were not enabled here.

FEX emitting these instructions proves reachability, not their frequency in Hollow Knight. The parent-owned 4451 diagnostic samples exact instructions on cache hits as well as misses; the relevant opcode match is `word & 0xffffffe0 == 0xd51b4400`. Do not infer importance from a static occurrence count or from Madeira's different Windows ARM64EC workload.

## Validation completed locally

`test-candidate.py` first checks the captured source hashes. A tiny compiled generator uses the real register mapping, `read_greg`, `mov_imm64`, `emit.h`, and JIT context layout to emit the candidate for every source register. It does not build or run LinPad. Those exact instruction words are tested in two ways:

- A small instruction interpreter passed 6,304 source-register/value cases, including all single-bit values, high bits, the allowed mask, its complement, random inputs and XZR. It checks full shadow, cached/applied mask, all guest registers/SP and context pointer. Removing the cache store or mask is detected by negative controls.
- An isolated Mac ARM64 assembly wrapper executed the emitted words in 4,672 cases with zero failures. It checked positive/negative rounding in all four modes, shadow and cache values, observed hardware FPCR against an independent masked control, valid FPSR and NZCV, every guest GPR/SP, and restoration of the host FPCR. Disallowed bits are present in several test inputs and do not reach hardware.

The first harness draft used reserved FPSR bit 31 as a preservation sentinel. Hardware correctly read it as zero. The test was corrected to architectural QC bit 27; no candidate instruction changed. This was a harness expectation error, not a failing candidate semantic case.

The wrapper models entry/exit enough to exercise these emitted words safely; it is not the full production trampoline or dispatcher. No whole-game test or performance comparison has occurred. These results establish a locally tested candidate, not integration readiness or a speedup.

Reproduce on the same source snapshot, locally on a Mac with ARM64:

```sh
python3 work/performance/fpcr-candidate/test-candidate.py
```

`prepare-candidate.py` regenerates the unapplied patch and records a new source snapshot. Run it only intentionally after reviewing a source change; the test itself refuses changed source hashes. Generated native test executables stay in `local-test/` and are not deployment artifacts.

## Remaining gates

1. Inspect 4451 samples and distinguish frequent FPCR writes from other unsupported instructions and cache invalidation/miss causes.
2. If justified, review/apply the one-file patch in a later build, preserving 4451 as its unchanged diagnostic baseline.
3. Run a complete LinPad regression covering immediate rounding, a syscall boundary, a forced unrelated gadget fallback, subsequent rounding and per-thread FPCR independence; compare against the gadget engine. Confirm rebuilt translator identity invalidates stale persistent translations (`pcache.c` salts entries with `jit_translator_build`).
4. Run matched physical game scenes and audio observations with the same rendering/runtime settings. Report measured result separately from successful startup and instruction semantics.

The broader source audit is in `runtime-options-audit.md`.
