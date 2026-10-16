#!/usr/bin/env python3
"""Curate the committed per-context SS cache and narrow real CLI controls."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

PERF=Path(__file__).resolve().parent
WORK=PERF.parent
REPO=WORK/'LinPad'
INPUT=PERF/'jit-ss-context-cache'
OUT=INPUT/'curated'
DEST=REPO/'installation/linpad-steam/performance-2026-10-16/jit-ss-context-cache'
FIX=subprocess.check_output(['git','rev-parse','f93f1449'],cwd=REPO,text=True).strip()
DOCS=subprocess.check_output(['git','rev-parse','0afc2925'],cwd=REPO,text=True).strip()
OUT.mkdir(exist_ok=True)
records=[]
def sha(data):return hashlib.sha256(data).hexdigest()
def save(name,data):
    if isinstance(data,str):data=data.encode()
    p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
def cp(source,name):
    data=source.read_bytes();save(name,data)
    records.append({'source':str(source.relative_to(WORK.parent)), 'destination':name,
                    'sha256':sha(data),'bytes':len(data),'mode':'byte-identical'})

cp(Path(__file__),Path(__file__).name)
for name in ['README.md','independent-graphics-review.md','independent-graphics-review.json',
             'regression.txt','benchmark.txt','lookup-benchmark.txt','context-benchmark.txt',
             'provenance.json','jit-ss-index-harness.c']:
    cp(INPUT/name,'host/'+name)
cp(INPUT/'source-provenance.json','host/precommit-source-provenance.json')
for name in ['jit/jit.c','jit/jit_internal.h','tests/manual/steam-compat/test-jit-ss-index.py',
             'tests/arm64-insn/smc.c','tests/manual/steam-compat/jit-ss-smc-probe.c']:
    data=subprocess.check_output(['git','show',FIX+':'+name],cwd=REPO)
    save('source/'+name,data)
    records.append({'source_commit':FIX,'source_path':name,'destination':'source/'+name,
                    'sha256':sha(data),'bytes':len(data),'mode':'exact committed blob'})
save('source-change.patch',subprocess.check_output(['git','show','--format=fuller','--no-ext-diff',FIX],cwd=REPO))
for name in ['jit/DESIGN.md','tests/manual/steam-compat/README.md']:
    data=subprocess.check_output(['git','show',DOCS+':'+name],cwd=REPO)
    save('documentation/'+name,data)
    records.append({'source_commit':DOCS,'source_path':name,'destination':'documentation/'+name,
                    'sha256':sha(data),'bytes':len(data),'mode':'exact separately committed documentation blob'})
save('documentation-change.patch',subprocess.check_output(['git','show','--format=fuller','--no-ext-diff',DOCS],cwd=REPO))
host=json.loads((INPUT/'provenance.json').read_text())
assert host['source_sha256']==sha((OUT/'source/jit/jit.c').read_bytes())
assert host['header_sha256']==sha((OUT/'source/jit/jit_internal.h').read_bytes())
review=json.loads((INPUT/'independent-graphics-review.json').read_text())
for name,digest in review['files'].items():assert digest==sha((OUT/'source'/name).read_bytes())
controls=INPUT/'runtime-controls'
for name in ['README.md','smc-provenance.json','smc-results.json',
             'smc-jit.txt','smc-tier2.txt','fallback-smc-jit.txt','fallback-smc-tier2.txt',
             'after-ss-context-mono-0-before-excerpt.txt','after-ss-context-mono-3221-before-excerpt.txt',
             'after-ss-context-mono-provenance.json','after-ss-context-mono-results.json']:
    cp(controls/name,'runtime-controls/'+name)
omitted=[]
for name in ['after-ss-context-mono-0-before.txt','after-ss-context-mono-3221-before.txt']:
    p=controls/name;data=p.read_bytes()
    omitted.append({'source':str(p.relative_to(WORK.parent)),'bytes':len(data),'sha256':sha(data),
                    'reason':'large raw local syscall trace; narrow excerpt archived'})
smc=json.loads((controls/'smc-provenance.json').read_text())
mono=json.loads((controls/'after-ss-context-mono-provenance.json').read_text())
cli_hash=smc['files']['work/linpad-cli/ish']
assert cli_hash==mono['files']['work/linpad-cli/ish']==sha((WORK/'linpad-cli/ish').read_bytes())
assert cli_hash=='b47c299a4da7445ee0aa2ba8dd5accd6ac682d2425a6af125e682988a348ef35'
for path,digest in smc['files'].items():
    if path.startswith('work/LinPad/'):
        name=path.removeprefix('work/LinPad/')
        assert digest==sha(subprocess.check_output(['git','show',FIX+':'+name],cwd=REPO))
for r in json.loads((controls/'smc-results.json').read_text()):
    assert r['exit_code']==0 and r['expected_result_observed']
for r in json.loads((controls/'after-ss-context-mono-results.json').read_text()):
    assert r['exit_code']==0 and not r['host_terminated_after_timeout']
    assert 'MONO_JIT_INIT_END domain=0x' in r['excerpt']
log=INPUT/'cli-build.log';raw=log.read_bytes();lines=raw.decode(errors='replace').splitlines()
excerpt='\n'.join(lines[:3]+lines[-8:])+'\n'
save('runtime-controls/cli-build-excerpt.txt',excerpt)
records.append({'source':str(log.relative_to(WORK.parent)),'destination':'runtime-controls/cli-build-excerpt.txt',
    'raw_sha256':sha(raw),'raw_bytes':len(raw),'archived_sha256':sha(excerpt.encode()),'mode':'first3/last8 build-log lines'})
save('provenance.json',json.dumps({'source_commit':FIX,'documentation_commit':DOCS,'cli_sha256':cli_hash,
    'reviewed_source_matches_commit':True,'host_regression':'438495 assertions with ASan/UBSan; invalidation/growth/context benchmarks pass',
    'local_controls':'ordinary/fallback ARM64 SMC in JIT and tier2; default-FEX exact Mono noSDL/fullSDLdummy; all exit0',
    'app_and_device':'tracked separately under build4450; not inferred from CLI results',
    'gameplay_and_fps':'not qualified by this archive',
    'precommit_analysis':'host/precommit-source-provenance.json preserves the earlier HEAD and pending documentation hashes; production/test blobs are pinned above.',
    'omitted_raw_logs':omitted,'files':records},indent=2)+'\n')
save('README.md','''# Per-context fallback instruction cache

Commit f93f1449 adds a 256-entry persistent single-step cache to each JIT
context. It follows the separate physical build4449 presentation-stage profile
that identified shared ss_get mutex waiting and wakeups across game threads.
That profile belongs to build4449, not to this new source or build4450.

Each entry stores the full guest PC, block pointer and mm generation. The slow
path fills only persistent shared entries while holding the existing mm lock.
One-shot, allocation-failure and generation-mismatch results remain excluded.
Hits retain the existing epoch lifetime and invalidation-generation contract.
Address-space pointer/id changes and reactivation after idle trim clear the
cache. The fixed assembly header/TLB offsets remain unchanged; sizeof-based
context allocation and persistent cache layout salt account for the added
6 KiB payload. Page indexing and shared-table growth remain intact.

Independent source review found no blocking correctness issue. The actual-
function host harness passes 438,495 assertions with ASan/UBSan, including
an actually freed stale block rejected by generation, PC collisions, a
generation change immediately after unlocking, mm/id/trim resets, one-shot
exclusion and allocator failure. Existing invalidation/growth controls pass.
Eight host threads perform 400,000 persistent hits: the shared path takes
400,000 mutex locks and the warmed local path takes zero. This proves the
mechanism on the synthetic workload, not the actual game cache hit rate,
device performance or FPS.

The rebuilt local CLI hash is
b47c299a4da7445ee0aa2ba8dd5accd6ac682d2425a6af125e682988a348ef35.
Its recorded JIT source hashes match f93f1449. Unchanged real ARM64 ordinary
SMC and actual MSR-FPCR fallback SMC both pass in normal JIT and forced tier2,
with 200 rewrites each. Exact default-FEX Mono noSDL and full embedded Unity
SDL0x3221 with local dummy video return nonnull domains and exit0 (2.482 and
3.101 seconds respectively). These timings are local functional observations
under shared host load, not an iPad speed comparison.

source/ contains exact committed production/test and unchanged SMC control
sources; source-change.patch is the committed change. host/ preserves the
earlier analysis, independent review and extracted harness. Its precommit
provenance records the earlier HEAD plus then-pending documentation drafts;
it does not change the exact source pin. The finalized prose was committed
separately as 0afc2925 and its exact blobs and patch are now included under
documentation/. That later documentation commit is not the app source pin.
runtime-controls/ contains small own-process
logs and narrowed Mono excerpts. Large syscall traces retain size/hash records
but remain local, as do CLI binaries and proprietary Unity/Mono/game bytes.

Build/sign/install/physical validation of build4450 are separate parent-owned
gates. Existing build4449 screenshots and cadence cannot be attributed to this
new cache. No game rendering, sound, playability or physical FPS is inferred.
''')
save('SHA256SUMS',''.join(sha(p.read_bytes())+'  '+str(p.relative_to(OUT))+'\n'
    for p in sorted(OUT.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
shutil.copytree(OUT,DEST,dirs_exist_ok=True)
for directory in [OUT,DEST]:
    for line in (directory/'SHA256SUMS').read_text().splitlines():
        digest,name=line.split('  ',1);assert sha((directory/name).read_bytes())==digest
    for p in directory.rglob('*.json'):json.loads(p.read_text())
print(json.dumps({'files':sum(p.is_file() for p in OUT.rglob('*')),
    'bytes':sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file()),
    'checksum_sha256':sha((OUT/'SHA256SUMS').read_bytes())},indent=2))
