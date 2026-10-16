# Single-step invalidation cost

The five-second Time Profiler capture from physical build 4447 contains 5,592
samples with 5.592 seconds of aggregate sample weight. Of that, 5.346 seconds
(95.600858%) are self samples in `ss_invalidate_locked`. This is CPU work in
the LinPad fallback cache, not a measurement of GPU shader work or frame rate.
Main FEX and Unity graphics threads both spend most of their samples here.

The previous implementation scans all 256 PC buckets and every cached
single-instruction fallback entry for each page invalidation. Ordinary data
writes can reach that scan when they collide with the 64-bit fallback bloom
filter. The trace proves the scanning cost; it does not itself measure bloom
saturation or the exact number of live fallback entries.

The fix adds a second index with 65,536 page buckets. Page invalidation visits
only the corresponding bucket; the exact `PAGE(pc)` comparison excludes hash
collisions. Ranges covering at least the bucket count scan all buckets once.
The existing PC lookup table stays unchanged. The new array costs 512 KiB per
address space and the links cost 16 bytes per cached entry on this 64-bit host.

Both indexes are mutated under the existing `mm->lock`. Each cached node's
predecessor link points into the fixed-size PC table or another live node.
Unlink repairs the successor before reusing `next` for deferred retirement.
Page links never own or free nodes; existing PC-table and jetsam reclamation
retain ownership. One-shot and allocation-failure paths never enter either
cache index. Constructor failure and address-space teardown free the array.

Removing fallback entries now also increments the cache generation. A
translator that observed the older generation cannot publish its in-flight
fallback into the cache afterward. The existing one-shot lifetime remains.
The original `PAGE(pc)` matching semantics are retained; this does not expand
support for misaligned instructions crossing guest pages.

`test-jit-ss-index.py` extracts actual production functions into a host
fixture. It passes 240,144 assertions with AddressSanitizer and UBSan:
half-open/high-address/all ranges, hash collisions, page-boundary PCs,
multiple PCs on one page, PC-chain predecessor repair, random survivor
comparison, bloom reset, cached/one-shot retirement, allocation failure and
a deterministic generation-change interleaving during translation.
Translation, compiled-block removal and final reclamation are stubbed;
constructor failure cleanup is source-reviewed, not fault-injected here.
Real guest self-modification is a separate integration test. The unchanged
`tests/arm64-insn/smc.c` passed 200 generated-function rewrites in both normal
JIT and forced tier 2, with `smc ok` and exit 0. The logs are under
`work/performance/ss-invalidation-review/smc-{jit,tier2}.txt`; this checks
compiled execution, not necessarily a forced unsupported-instruction fallback.
The separate `jit-ss-smc-probe.c` alternates `MSR FPCR` instruction encodings
that use the fallback engine. Both modes also pass its 200 rewrites; actual
JIT counters record 201 fallbacks, 200 invalidations and 200 retired
single-step entries. That source was committed separately as `e334e1b1`.

The synthetic benchmark holds 65,536 entries and requests 1,000 invalidations
for nonmatching high pages that collide in the bloom and page indexes.
The old function is verified byte-for-byte against the prior source except
its name. It examines 65,536,000 nodes; the new function examines 1,000, and
both retain all 65,536 entries. This local run took about 1.892 seconds for
the old scan and 4 microseconds for the index. The tiny indexed duration is
near the host timer's resolution and is not a reliable game-speedup ratio.
Physical game startup and presentation still require build 4448 validation.

Run the host test from the workspace:

```
python3 work/LinPad/tests/manual/steam-compat/test-jit-ss-index.py --out work/performance/jit-ss-index
```

The saved `summarize-time-profile.py` reads the existing exported XML and
reproduces the profile counts. Raw trace/XML/TOC files stay local because
they contain device and container identifiers. `profile-summary.json`
retains function names, bounded target-thread names and the raw input hash.
