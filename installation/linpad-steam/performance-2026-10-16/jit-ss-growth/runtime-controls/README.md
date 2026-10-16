# Combined SS-lookup growth and scheduler-query controls

The parent rebuilt the local CLI with SS-lookup growth commit df8a7720 and
the frozen scheduler-priority query repair. This run changes no test binary,
launcher or runtime settings. Exact source/binary hashes are in the provenance.

All four real ARM64 code-modification cases passed: the existing ordinary SMC
and the fallback MSR-FPCR SMC, each in normal JIT and forced tier 2. The fallback
probe reports 200 correct rewrites, 201 fallbacks and 200 invalidations.

The unchanged exact Mono probe returned a nonnull domain and exit 0 both with
no SDL (2.076 seconds) and after embedded Unity SDL flags0x3221 (2.798 seconds).
The SDL control uses the local dummy video backend. FEX settings and the probe
are unchanged from the preceding control. These elapsed times are local
functional observations, not an iPad startup ratio or a gameplay/FPS result.

The ordinary SMC, fallback SMC and Mono probe hashes were checked against the
previously served binaries. Both provenance records identify the same rebuilt
CLI. Proprietary Unity/Mono/mscorlib bytes are not included in this folder.
