# Build 4451 fallback samples: LRCPC2

The report `reports/ss4451-hollow-startup-1.txt` contains two recent sample-ring snapshots for the same running game, PID 348. Every one of its 1,223 retained sample records matches `(word & 0x3f200c00) == 0x19000000`, the LDAPUR/STLUR family. The main thread contributes 64 records in each snapshot: all 128 match, and none is MSR FPCR. Records from other contexts can repeat across snapshots; 1,223 is a count of retained report records, not 1,223 independent random samples or a total execution count.

Apple LLVM's AArch64 disassembler, enabled for `+rcpc-immo`, confirms the instructions. `sample-disassembly.txt` and `sample-counts.json` preserve the exact input-word order and decoded output.

| Word | Decoded instruction | Main retained occurrences |
| --- | --- | ---: |
| `99400174` | `ldapur w20, [x11]` | 13 |
| `994703b4` | `ldapur w20, [x29, #112]` | 10 |
| `d9400294` | `ldapur x20, [x20]` | 6 |
| `991c0134` | `stlur w20, [x9, #-64]` | 5 |
| `99000087` | `stlur w7, [x4]` | 5 |
| `d95a0125` | `ldapur x5, [x9, #-96]` | 4 |
| `d9410124` | `ldapur x4, [x9, #16]` | 3 |
| `19400294` | `ldapurb w20, [x20]` | 2 |

These are acquire/release memory operations, not floating-point control writes. The separate FPCR candidate remains unapplied and is deprioritized by this evidence.

## Why these instructions fall back

LinPad advertises LRCPC2 when host and engine support it. `asbestos/guest-arm64/gen.c::gen_ldst` explicitly recognizes this family and rewrites it to LDUR/STUR with a DMB before stores or after loads. That is a functional gadget implementation.

`jit/translate.c::tr_ldst` has a fast path for LDAPR and ordered LDAR/STLR, but no matching LRCPC2 immediate family. Thus those FEX-generated memory operations repeatedly leave fast translated code and invoke gadget single-step. This is a missing fast decoder path, not evidence that the hardware lacks LRCPC2.

FEX `MemoryOps.cpp::LoadMemTSO` uses LDAPUR when `SupportsTSOImm9` is true, and `StoreMemTSO` uses STLUR. `Addressing.cpp::SelectAddressMode` folds a signed nine-bit displacement into those operations only when that same feature is true.

## Immediate controlled alternative: exact existing configuration

Use this environment variable for a **new FEX game process**:

```sh
export FEX_HOSTFEATURES=disablelrcpc2
```

Then use a uniquely named copy of the same launch script/log paths so the baseline evidence is not overwritten. The already-running game cannot change its generated code through a different shell's environment.

This exact spelling is verified against the checked-out source and the generated configuration used to build the installed patched FEX:

- `Config.json.in` declares HostFeatures as `strenum` and `DISABLELRCPC2` as `disablelrcpc2`.
- Generated `build/include/FEXCore/Config/ConfigOptions.inl` parses `FEX_HOSTFEATURES`, maps that string to `DISABLELRCPC2 = 1ULL << 9`, and calls `EnumParser`.
- `Config.h::EnumParser` recognizes the name, and `Source/Common/Config.cpp::EnvLoader` loads that environment option.
- `HostFeatures.cpp::OverrideFeatures` applies DISABLELRCPC2 only to `SupportsTSOImm9`. It leaves `SupportsRCPC`, TSOEnabled, and the remaining feature choices unchanged.
- The actual served `FEX-apple-m1-munmap` contains `FEX_HOSTFEATURES` and `disablelrcpc2` strings, and its SHA matches the installed patched binary provenance.

With LRCPC2 disabled but LRCPC retained, FEX GPR loads use LDAPR and stores use STLR. The address-selection code materializes offsets separately. Both instruction families already have LinPad fast translation paths. Acquire/release memory ordering is retained; this does **not** disable TSO. Do not additionally disable LRCPC for the first control.

The current `launch-game`, `fex-hollow-egl`, and `fex-game-crash-report` scripts do not unset FEX_HOSTFEATURES. Therefore an export before their normal launch chain survives environment cleanup. Setting it in a final wrapper immediately before FEX is also valid. No new FEX binary or app build is required for this experiment, and unsetting the variable in a subsequent launch restores default feature detection. It does not modify an account, shared configuration file, or persistent launcher default.

The first validation should use the unchanged graphics/runtime/audio settings and a distinct log namespace, then inspect the same fallback diagnostic and a matched visible scene. Verify the emitted LRCPC2 family vanishes/reduces and report any new top fallback family. Startup success and a lower fallback rate do not independently prove playable frame rate.

## Production translation direction, if selected

A later fast decoder could handle this family via the existing memory emitter. It must preserve signed imm9 addressing, byte/halfword/word/doubleword widths, sign-extending variants, XZR/SP rules, acquire/release ordering, page-change synchronization and helper/fault behavior. A plain LDUR/STUR without ordering is not sufficient. Directly passing the guest address to host LDAPUR/STLUR is also invalid because guest memory still needs translation.

This audit does not implement that production change. The parent can compare the reversible feature override first and assign the translator work independently. No performance improvement is claimed before its actual run.
