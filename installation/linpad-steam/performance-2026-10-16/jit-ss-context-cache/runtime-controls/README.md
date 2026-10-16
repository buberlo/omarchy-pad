# Per-context SS cache: actual local runtime controls

The parent-authorized local CLI rebuild completed after independent review.
The changed header/layout compiled in jit.c, translate.c, codemem.c and pcache.c;
the existing TLB-offset assertion passed. No physical app was built here.

Unchanged ordinary ARM64 SMC and actual fallback MSR-FPCR SMC both pass in normal
JIT and forced tier2, all exit0. The fallback reports200 correct rewrites,
201fallbacks and200invalidations. Exact default-FEX Mono0 and embedded Unity
SDL0x3221+Mono both return nonnull domains and exit0 (2.482s and3.101s local).
SDL uses the dummy local video driver. These are functional observations, not
an iPad performance comparison; there were other host activities.

Binary hashes match the preceding served ordinary/fallback/Mono probes, and
both provenance records identify the same rebuilt CLI. Proprietary runtime
bytes are not included. No device operation or commit was performed here.
