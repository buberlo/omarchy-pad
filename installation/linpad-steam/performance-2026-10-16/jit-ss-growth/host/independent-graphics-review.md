# Independent review of fallback PC-table growth

No blocking production correctness issue found in `jit/jit.c` and
`jit/jit_internal.h`. This review is read-only; implementation belongs to the
memory agent. Physical performance qualification remains a separate gate.

- All live PC-table reads and writes are under `mm->lock`. The previously
  outside-lock `ss_get` bucket calculation is now inside, so it cannot use
  a size from one table against another. The only other table walk is the
  quiescent address-space destructor.
- Rehash retains each node's old `next` before changing it. Each moved node
  points `hash_prev` to the new bucket head, and any prior bucket head is
  repaired to point at the new predecessor's `next`. Thus every live backlink
  is repaired before the old array is freed. The page index and node/block
  addresses remain unchanged.
- The old bucket array needs no epoch retirement because no runtime reader
  accesses it outside the lock. Existing entries and compiled blocks retain
  their previous deferred-reclamation lifetime. One-shot entries are still
  unindexed and unaffected by growth.
- Geometric growth starts at a power-of-two size and preserves that invariant
  for `pc_hash`. The guard before doubling bounds both multiplication and
  allocation byte size. Saturating the next retry threshold avoids count
  doubling overflow. Actual allocations cannot approach the synthetic
  maximum-count test's fictitious table size.
- Allocation failure returns without altering the old table or backlinks.
  Retry waits until the live count doubles, avoiding an allocation attempt
  for every later instruction. A later successful retry grows repeatedly
  until capacity exceeds the count. Invalidation lowering the count does
  not invalidate this conservative retry policy.
- Structural table growth does not change guest code, so it requires no
  generation increment. The earlier page invalidation generation fix and
  page index remain intact.

The actual-function harness reports 338,461 assertions with ASan/UBSan,
including eight growth steps, distinct block identity per PC, invalidation
and reinsertion after growth, allocation-failure backoff/recovery, an overflow
guard and a concurrent hit reader during growth. Four high-address stride
patterns pass the lookup workload. These are local fixture results, not a
physical game speed measurement.

One test-only review note was resolved: the older invalidation benchmark also
populates via `add`, which now enables growth. Disable growth for that
fixture to retain the historical 256-bucket old-scan control. The new lookup
benchmark already disables growth in its old-table control correctly. The
implementation owner applied the control correction and reran the complete
host harness and both benchmarks successfully before freezing the files.
