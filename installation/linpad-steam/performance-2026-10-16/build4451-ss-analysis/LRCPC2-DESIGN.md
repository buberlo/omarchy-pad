# Native LDAPUR/STLUR support: design only

No translator change, test, or performance claim is made here. First evaluate the reversible `FEX_HOSTFEATURES=disablelrcpc2` control on physical build 4451. It already selects translated LDAPR/STLR instructions and may resolve the measured hot family without another app build.

## Existing behavior and narrow implementation boundary

`jit/translate.c:tr_ldst` has no LRCPC2 case. The gadget decoder in `asbestos/guest-arm64/gen.c` recognizes `(insn & 0x3f200c00) == 0x19000000`, lowers to the corresponding LDUR/STUR scalar form, and adds `dmb ish` before a release store or after an acquire load. The barrier gadget is in `asbestos/guest-arm64/gadgets-aarch64/memory.S`. Matching this already-used lowering is a conservative first implementation, rather than directly emitting host LRCPC2 instructions and introducing a host-feature requirement.

Add a separate family case near the beginning of `tr_ldst`, before the ordinary single-register decoder. Decode size, opc, signed imm9, Rn and Rt. Validate allocated scalar combinations explicitly: stores for sizes 8/16/32/64; zero-extending loads for all four sizes; sign-extending byte/halfword loads to W or X; sign-extending word loads to X. Reject size=3 with opc=2/3 and size=2 with opc=3. In this family, the reserved size=3/opc=2 form must not inherit the ordinary decoder's PRFM no-op interpretation.

Construct the existing `memop` with `AK_IMM`, `imm=sign_extend(imm9,9)`, `size=1<<size`, no writeback, and existing scalar native load/store encodings with effective host offset zero. Retain opc so hardware performs the correct sign extension or W-register zero extension. Set the data operand to read for stores and write for loads; preserve Rn=31 as SP and Rt=31 as zero/discard. Use `emit_mem` for guest-address translation, operand renaming, temporary/spilled registers, and original-PC fault maps. No endian conversion is required in the current little-endian ARM64 guest/host configuration.

## Ordering and faults

Set `m.sync=true` to retain the existing page-table-change/CoW TLB-coherence check used by acquire/release memory operations. This flag is not a CPU memory barrier. Emit a real `dmb ish` before stores and after successful loads, matching the gadget path's stronger ordering. All successful inline, compact-stub, and cold-helper branches must reach the appropriate barrier; a load fault must retain the original destination and guest PC. Review emitted control flow, rather than assuming a barrier added to one fast path covers the helper paths.

Do not blindly set `m.atomic=true`: the current `JH_MEM` helper rejects every page-crossing access marked atomic. The existing gadget lowering uses ordinary scalar accesses, so that would change valid cross-page behavior. A conservative lowering uses the ordinary access path plus barriers. Existing alignment, split-page, and permission semantics must be compared with the gadget baseline before acceptance; no broader architectural alignment guarantee is asserted here.

For compact tier-1 stores, the current helper can defer committing a cross-page bounce buffer until the next memory helper or dispatcher exit. It flushes the TLB so the next guest memory access enters the helper and commits first. This existing mechanism needs a targeted release-store ordering test and control-flow review. If the release semantics cannot be proved for that deferred path, force an immediate commit for this new store family instead of weakening ordering. Do not change general bounce behavior as part of the initial patch without a demonstrated need.

Preserve null, PROT_NONE, read-only, wraparound, partial-access and SMC handling by reusing `emit_mem`. Verify write invalidation remains on the existing TLB/helper route; changing the opcode family must not bypass protection or invalidate fewer translated pages. Stronger barriers are semantically conservative but have a cost; the expected saving is avoiding fallback dispatch, shared-cache locking and full guest-state copies, not eliminating memory translation or ordering costs.

## Focused acceptance tests

1. Exercise the actual translator with the exact sampled words and every allocated size/opc combination. Assert reserved combinations remain unsupported, signed offsets cover -256/0/255, and the emitted memory instruction preserves width/signedness and original-PC mapping.
2. Run real ARM64 guest probes for negative signed values, W zero extension, SP bases, zero/discard registers, Rt=Rn, and spilled registers x11–x15/x18. Check values and surrounding memory, not just translation success.
3. Compare gadget, normal JIT, compact tier 1 and tier 2 for valid page crossings plus null, PROT_NONE, read-only and end-of-address-space faults. Check signal address, PC and destination preservation. Include a release store crossing a page boundary immediately followed by another guest memory operation and by block exit.
4. Add a bounded two-thread release/acquire message-passing test, inspect the emitted barrier sequence, and retain existing real self-modifying-code controls. A passing stress test alone is not proof of every Arm memory-model behavior.
5. On the physical app, use the same opt-in counters to verify this family no longer enters the fallback path. Then disable diagnostics and compare the same foreground game/menu scene and compositor-delivery measurement. Startup milestones, menu cadence and playable gameplay remain distinct gates.

Arm documents LRCPC2 as the extension adding unscaled-offset and sign-extending acquire forms; LLVM's upstream code-generation tests cover these width and sign-extension variants. References: [Arm RCpc overview](https://developer.arm.com/community/arm-community-blogs/b/tools-software-ides-blog/posts/enabling-rcpc-in-gcc-and-llvm), [Arm LDAPURSW reference](https://support.arm.com/documentation/ddi0602/2023-09/Base-Instructions/LDAPURSW--Load-Acquire-RCpc-Register-Signed-Word--unscaled--), [LLVM LRCPC2 implementation and tests](https://lists.llvm.org/pipermail/llvm-commits/Week-of-Mon-20230925/1216812.html). Local source hashes are in `provenance.json`.
