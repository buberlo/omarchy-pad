# Build 4450 CPU profile boundary

These stable aggregates analyze two already-recorded, bounded own-app profiles.
The startup interval is 13:14:45.059–13:14:50.844 UTC. The separate loading
interval is 13:17:31.201–13:17:37.094 UTC, before the title-menu observation.
The latter has 15,343 samples and 15.343 weighted CPU seconds across a
5.557-second sample span. Weighted CPU samples across threads are not wall time.

Loading still spends 5.103 weighted seconds (33.26%) in mutex wait/wake beneath
ss_get; its inclusive share is 40.28%. CPU-state copies surrounding gadget
fallback consume 13.26%. These categories overlap. The sample cannot tell
whether context-cache misses come from generation changes, collisions or the
working set. No unmeasured cause or gameplay speedup is asserted.

source-audit.md is the unchanged independent source analysis. Its references
to original TOCs describe local inputs: only their timing fields and hashes
are included as trace-timing.json here. Raw trace/XML samples, device metadata
and proprietary game bytes are excluded. provenance.json and aggregate files
retain the original input/source hashes. analyze-paths.py can recompute path
categories when given the matching local exported sample XML.

This loading profile is separate from the later 30-second static-menu
compositor cadence sample. It is not a profile of that menu interval.
