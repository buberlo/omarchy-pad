# Per-context persistent single-step lookup cache

Physical build4449 made PC-table lookup short, but the later presentation-stage
profile showed8.733weighted seconds in ss_get mutexwait and5.520 in its unlock
wake path out of19.681weighted sample seconds (44.37%+28.05%). Main, graphics,
preloader and worker threads repeatedly enter the same per-mm mutex. The
profile is archived separately in build4449-profile-audit; it is not a profile
of this uninstalled change.

The new fast path stores256 persistent shared SS blocks per jit_ctx (6KiB),
keyed by guest PC and mm->gen. The slow path captures that generation while
holding the existing mm mutex. One-shot, allocation-failure and generation-
mismatch results cannot fill the local cache. The previous page index,
shared-table growth, generation increments and deferred epoch reclamation
remain in force. All mm/id switches clear the cache, including reactivation
after idle trim, whose existing null-mm reset enters the same helper.

438495ASan/UBSan assertions pass in the extracted-production-function fixture.
Coverage includes repeated lock-free hits, collisions, an actually freed stale
block rejected after a generation change, generation movement on unlock,
failed allocation and one-shot exclusion, generation movement during code
generation, mm switches/id reuse and trim-reset integration. Existing growth,
backlink, range and concurrent lookup tests also remain passing.

An eight-thread benchmark performs400000persistent hits with either the actual
shared lookup or the actual warmed per-context fast path. The former takes
400000mutex locks; the latter takes zero. Both execute under sanitizers too.
This synthetic hit pattern establishes the intended mechanism, not actual
game hit rate, device throughput or FPS. The fixture stubs translation and
compiled-block handling; its quiescent-block-free case models an already
completed epoch and does not substitute for real SMC/device validation.

Independent review passed. The authorized local CLI rebuild and unchanged
real SMC/Mono controls passed; see runtime-controls. Physical build/install
and device qualification remain parent-owned gates.
No game, proprietary binary or credentials are included here.
