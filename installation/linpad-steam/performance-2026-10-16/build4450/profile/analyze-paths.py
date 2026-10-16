#!/usr/bin/env python3
"""Read an exported own-app time-profile; aggregate exact paths, no device calls."""
import collections, hashlib, json, pathlib, sys
import xml.etree.ElementTree as ET

src, dest = map(pathlib.Path, sys.argv[1:])
raw = src.read_bytes()
root = ET.fromstring(raw)
ids = {e.get('id'): e for e in root.iter() if e.get('id')}
def resolve(e):
    while e is not None and e.get('ref'):
        e = ids[e.get('ref')]
    return e

paths, sites, symbols, threads, threadpaths, categories = (collections.Counter() for _ in range(6))
total = 0
for row in root.iter('row'):
    w = int(resolve(row.find('weight')).text)
    total += w
    trace = resolve(row.find('tagged-backtrace'))
    frames = [] if trace is None else [resolve(f) for f in trace.findall('frame')]
    names = tuple(f.get('name', '?') for f in frames)
    if not names:
        continue
    thread = resolve(row.find('thread')).get('fmt', '?')
    symbols[names[0]] += w
    threads[thread] += w
    if names[0].startswith('0x'):
        categories['numeric_unsymbolicated_or_generated_self'] += w
    if names[0] == '_platform_memmove' and 'gadget_step' in names:
        categories['memmove_under_gadget_step'] += w
    if names[0].startswith('__psynch_mutex') and 'ss_get' in names:
        categories['mutex_kernel_under_ss_get'] += w
    if 'ss_get' in names:
        categories['ss_get_inclusive'] += w
    if names[0].startswith('gadget_'):
        categories['gadget_named_self'] += w
    if any(x in names[0] for x in ('memmove', 'mutex', 'ss_get', 'lookup_lockfree', 'fiber_', 'gadget_', 'mincore')):
        paths[names[:12]] += w
        threadpaths[(thread, names[:8])] += w
    if names[0] == '_platform_memmove':
        sites[tuple((f.get('name'), f.get('addr'), f.find('source').get('line') if f.find('source') is not None else None) for f in frames[:4])] += w

def entry(name, n):
    return {'name': name, 'seconds': n/1e9, 'percent': n*100/total}
result = {
    'input_sha256': hashlib.sha256(raw).hexdigest(),
    'scope': 'Weighted statistical CPU samples; categories can overlap; no elapsed-time or FPS claim.',
    'sample_weight_seconds': total/1e9,
    'categories': [entry(k, v) for k,v in categories.most_common()],
    'top_self': [entry(k,v) for k,v in symbols.most_common(100)],
    'threads': [entry(k,v) for k,v in threads.most_common()],
    'paths': [entry(list(k), v) for k,v in paths.most_common(60)],
    'thread_paths': [{'thread': t, **entry(list(k),v)} for (t,k),v in threadpaths.most_common(40)],
    'memmove_sites': [entry([{'name':n,'pc':a,'line':l} for n,a,l in k],v) for k,v in sites.most_common(15)],
}
dest.write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps({k: result[k] for k in ('sample_weight_seconds','categories','threads')}, indent=2))
