#!/usr/bin/env python3
"""Summarize an already-exported xctrace time-profile XML without private paths."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("samples", type=Path)
p.add_argument("--out", type=Path, required=True)
a = p.parse_args()
raw = a.samples.read_bytes()
root = ET.fromstring(raw)
ids = {e.get("id"): e for e in root.iter() if e.get("id")}


def resolve(e):
    while e is not None and e.get("ref"):
        e = ids[e.get("ref")]
    return e


self_counts = Counter()
inclusive = Counter()
threads = Counter()
thread_self = defaultdict(Counter)
paths = Counter()
times = []
total = samples = missing = 0
for row in root.iter("row"):
    samples += 1
    w = int(resolve(row.find("weight")).text)
    t = int(resolve(row.find("sample-time")).text)
    times.append(t)
    total += w
    thread = resolve(row.find("thread")).get("fmt", "unknown")
    threads[thread] += w
    trace = resolve(row.find("tagged-backtrace"))
    names = [] if trace is None else [resolve(f).get("name", "unknown") for f in trace.findall("frame")]
    if not names:
        missing += 1
        continue
    self_counts[names[0]] += w
    thread_self[thread][names[0]] += w
    for name in set(names):
        inclusive[name] += w
    if names[0] == "ss_invalidate_locked":
        paths[tuple(names[:7])] += w


def top(counter, limit):
    return [{"function": name, "weighted_seconds": n / 1e9, "percent": 100 * n / total}
            for name, n in counter.most_common(limit)]


result = {
    "input_sha256": hashlib.sha256(raw).hexdigest(),
    "scope": "Statistical CPU samples from the already-recorded own-app trace. Weighted sample time is not wall time or game FPS.",
    "samples": samples, "missing_backtrace_rows": missing,
    "sample_weight_seconds": total / 1e9,
    "sample_span_seconds": (max(times) - min(times)) / 1e9,
    "top_self": top(self_counts, 15), "top_inclusive": top(inclusive, 15),
    "threads": [{"thread": name, "weighted_seconds": n / 1e9, "percent": 100 * n / total,
                 "top_self": top(thread_self[name], 4)} for name, n in threads.most_common(10)],
    "ss_invalidation_paths": [{"frames": list(path), "weighted_seconds": n / 1e9}
                              for path, n in paths.most_common(8)],
}
a.out.write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps({k: result[k] for k in ("samples", "sample_weight_seconds", "sample_span_seconds", "top_self")}, indent=2))
