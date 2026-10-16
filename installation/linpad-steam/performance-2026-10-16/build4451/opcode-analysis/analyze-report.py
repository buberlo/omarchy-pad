#!/usr/bin/env python3
"""Analyze bounded LinPad SS diagnostic reports; no device operations."""
import collections, hashlib, json, pathlib, re, sys

source, target = map(pathlib.Path, sys.argv[1:])
raw = source.read_bytes()
lines = raw.decode().splitlines()
def fields(line):
    return {k: int(v, 16 if v.startswith('0x') or k == 'insn' else 10)
            for k,v in re.findall(r'(\w+)=(0x[0-9a-f]+|[0-9a-f]+)(?=\s|$)', line)}
game = int(next(l.split('=')[1] for l in lines if l.startswith('GAME_PID=')))
snapshots = {}; current = None; context = None; main_delta = None; maps=[]
for line in lines:
    if line.startswith('SNAPSHOT_BEGIN='):
        index = int(line.split('=')[1]); current = {'index':index,'contexts':[],'headers':{}}; snapshots[index]=current
    elif line.startswith('SNAPSHOT_END='):
        current = context = None
    elif current is not None:
        if line.startswith('context '):
            context = {'counters':fields(line),'samples':[]};current['contexts'].append(context)
        elif line.startswith('sample '):
            q=fields(line)
            assert context and q['tid']==context['counters']['tid'] and q['mmu']==context['counters']['mmu']
            context['samples'].append(q)
        elif line.startswith('Fri '): current['date_utc']=line
        else: current['headers'][line.split('=',1)[0].split(' ',1)[0]]=line
    elif line.startswith('MAIN_DELTA '):main_delta=fields(line)
    else:
        m=re.match(r'^([0-9a-f]+)-([0-9a-f]+)\s+(\S+)\s+([0-9a-f]+)\s+\S+\s+\d+\s*(.*)$',line)
        if m: maps.append({'start':int(m[1],16),'end':int(m[2],16),'permissions':m[3],'offset':int(m[4],16),'name':m[5]})
def family(insn):
    if insn & 0x3f200c00 == 0x19000000:return 'LRCPC2_LDAPUR_STLUR'
    if insn & 0xffffffe0 == 0xd51b4400:return 'MSR_FPCR'
    return 'other'
def summarize(c):
    total=sum(c.get(k,0) for k in ('hit','miss_cold','miss_collision','miss_generation'))
    misses=total-c.get('hit',0)
    return {'attempts':total,'misses':misses,
            'percent_of_attempts':{k:100*c.get(k,0)/total if total else 0 for k in ('hit','miss_cold','miss_collision','miss_generation')},
            'collision_percent_of_misses':100*c.get('miss_collision',0)/misses if misses else 0,
            'shared_hit_percent_of_misses':100*c.get('shared_hit',0)/misses if misses else 0}
for snapshot in snapshots.values():
    counts=collections.Counter()
    for context in snapshot['contexts']:
        c=context['counters'];context['role']='main' if c['tid']==game else 'worker'
        context['interpretation']='Cumulative counters since this context/mmu reset; no worker interval delta inferred.'
        context['summary']=summarize(c)
        freq=collections.Counter();families=collections.Counter();paths=collections.Counter();misses=collections.Counter()
        for q in context['samples']:
            assert q['readable'] in (0,1)
            f=family(q['insn']) if q['readable'] else 'unreadable'
            families[f]+=1;counts[f]+=1;freq[f'{q["insn"]:08x}']+=1;paths[q['path']]+=1;misses[q['miss']]+=1
            match=next((m for m in maps if m['start']<=q['pc']<m['end']),None)
            q['pc_hex']=hex(q['pc']);q['insn_hex']=f'{q["insn"]:08x}';q['family']=f
            q['mapping']=None if match is None else {**match,'start':hex(match['start']),'end':hex(match['end']),'pc_offset_in_mapping':hex(q['pc']-match['start']),'file_offset':hex(q['pc']-match['start']+match['offset']) if match['name'].startswith('/') else None}
        context['recent_sample_summary']={'count':len(context['samples']),'families':dict(families),'opcode_counts':dict(freq.most_common()),'path_counts':dict(paths),'miss_counts':dict(misses)}
    snapshot['all_context_recent_samples']=dict(counts)
gates={key:next((line.split('=',1)[1]for line in lines if line.startswith(key+'=')),None)
       for key in ('MAIN_DELTA_ELIGIBLE','PROCESS_CONTINUITY_ELIGIBLE','ALL_DELTA_GATES_VALID','MAPS_TRUNCATED','MAPS_READ_EXIT')}
result={'source':str(source),'source_sha256':hashlib.sha256(raw).hexdigest(),'game_pid':game,'gates':gates,
        'main_delta':main_delta,'main_delta_summary':summarize(main_delta) if main_delta and gates['MAIN_DELTA_ELIGIBLE']=='1' else None,
        'snapshots':list(snapshots.values()),'map_regions':len(maps),
        'limitations':['Recent64 per-context sample rings do not represent all attempts in the10-second counter interval.',
                       'Worker cumulative records are not differenced: identity fields cannot rule out context incarnation reuse.',
                       'Diagnostic counters measure attempts and approximate cross-field snapshots, not time or FPS.']}
target.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:result[k]for k in ('source_sha256','game_pid','gates','main_delta_summary')},indent=2))
for s in result['snapshots']:
    print('snapshot',s['index'],s['all_context_recent_samples'])
    for c in s['contexts']:
        if c['role']=='main': print('main recent',c['recent_sample_summary'])
