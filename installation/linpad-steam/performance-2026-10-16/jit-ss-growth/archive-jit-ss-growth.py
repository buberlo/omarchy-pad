#!/usr/bin/env python3
"""Curate the committed growth fix and narrow combined-CLI controls."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

PERF = Path(__file__).resolve().parent
WORK = PERF.parent
REPO = WORK / 'LinPad'
INPUT = PERF / 'jit-ss-growth'
OUT = INPUT / 'curated'
DEST = REPO / 'installation/linpad-steam/performance-2026-10-16/jit-ss-growth'
FIX = 'df8a7720618130c05d213a0002fc46968fb99a0a'
COMBINED = subprocess.check_output(['git', 'rev-parse', 'd8fa5c1e'], cwd=REPO, text=True).strip()
OUT.mkdir(exist_ok=True)
records = []


def sha(data): return hashlib.sha256(data).hexdigest()


def save(name, data):
    if isinstance(data, str): data = data.encode()
    path = OUT / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)


def cp(source, name):
    data = source.read_bytes(); save(name, data)
    records.append({'source': str(source.relative_to(WORK.parent)), 'destination': name,
                    'sha256': sha(data), 'bytes': len(data), 'mode': 'byte-identical'})


cp(Path(__file__), 'archive-jit-ss-growth.py')
for name in ['README.md', 'independent-graphics-review.md', 'regression.txt', 'benchmark.txt',
             'lookup-benchmark.txt', 'provenance.json', 'jit-ss-index-harness.c', 'swapchain-lock-paths.json']:
    cp(INPUT / name, 'host/' + name)
cp(INPUT / 'source-provenance.json', 'host/precommit-source-provenance.json')
for name in ['jit/jit.c', 'jit/jit_internal.h', 'jit/DESIGN.md',
             'tests/manual/steam-compat/test-jit-ss-index.py', 'tests/manual/steam-compat/README.md',
             'tests/arm64-insn/smc.c', 'tests/manual/steam-compat/jit-ss-smc-probe.c']:
    data = subprocess.check_output(['git', 'show', FIX + ':' + name], cwd=REPO)
    save('source/' + name, data)
    records.append({'source_commit': FIX, 'source_path': name, 'destination': 'source/' + name,
                    'sha256': sha(data), 'bytes': len(data), 'mode': 'exact committed blob'})
save('source-change.patch', subprocess.check_output(['git', 'show', '--format=fuller', '--no-ext-diff', FIX], cwd=REPO))

controls = INPUT / 'runtime-controls'
for name in ['README.md', 'smc-provenance.json', 'smc-results.json',
             'smc-jit.txt', 'smc-tier2.txt', 'fallback-smc-jit.txt', 'fallback-smc-tier2.txt',
             'after-ss-growth-mono-0-before-excerpt.txt', 'after-ss-growth-mono-3221-before-excerpt.txt',
             'after-ss-growth-mono-provenance.json', 'after-ss-growth-mono-results.json']:
    cp(controls / name, 'runtime-controls/' + name)
omitted = []
for name in ['after-ss-growth-mono-0-before.txt', 'after-ss-growth-mono-3221-before.txt']:
    data = (controls / name).read_bytes()
    omitted.append({'source': str((controls / name).relative_to(WORK.parent)),
                    'bytes': len(data), 'sha256': sha(data), 'reason': 'large raw local syscall trace; narrow excerpt archived'})
smc = json.loads((controls / 'smc-provenance.json').read_text())
mono = json.loads((controls / 'after-ss-growth-mono-provenance.json').read_text())
cli_hash = smc['files']['work/linpad-cli/ish']
assert cli_hash == mono['files']['work/linpad-cli/ish']
assert cli_hash == sha((WORK / 'linpad-cli/ish').read_bytes())
source_matches = []
for name, digest in smc['files'].items():
    if not name.startswith('work/LinPad/'): continue
    path = name.removeprefix('work/LinPad/')
    blob = subprocess.check_output(['git', 'show', COMBINED + ':' + path], cwd=REPO)
    assert sha(blob) == digest, path
    source_matches.append({'path': path, 'sha256': digest, 'matches_commit': COMBINED})
for item in json.loads((controls / 'smc-results.json').read_text()):
    assert item['exit_code'] == 0 and item['expected_result_observed']
for item in json.loads((controls / 'after-ss-growth-mono-results.json').read_text()):
    assert item['exit_code'] == 0 and not item['host_terminated_after_timeout']
    assert 'MONO_JIT_INIT_END domain=0x' in item['excerpt']

log = INPUT / 'combined-cli-build.log'; raw = log.read_bytes()
lines = raw.decode(errors='replace').splitlines()
excerpt = '\n'.join(lines[:3] + lines[-5:]) + '\n'
save('runtime-controls/combined-cli-build-excerpt.txt', excerpt)
records.append({'source': str(log.relative_to(WORK.parent)), 'destination': 'runtime-controls/combined-cli-build-excerpt.txt',
                'raw_sha256': sha(raw), 'raw_bytes': len(raw), 'archived_sha256': sha(excerpt.encode()),
                'mode': 'first3/last5 build-log lines'})

save('provenance.json', json.dumps({
    'growth_commit': FIX, 'combined_scheduler_commit': COMBINED,
    'combined_cli_sha256': cli_hash,
    'runtime_capture_head_before_scheduler_commit': smc['source_head'],
    'combined_sources_match_committed_blobs': source_matches,
    'host_regression': '338461 assertions with ASan/UBSan; invalidation and four-stride lookup benchmarks pass',
    'combined_local_controls': 'ordinary/fallback SMC in JIT and tier2; exact Mono noSDL/fullSDLdummy, all exit0',
    'app_and_device': 'tracked separately under build4449; not inferred from local CLI results',
    'gameplay_and_fps': 'not qualified by this archive',
    'omitted_raw_logs': omitted, 'files': records,
}, indent=2) + '\n')
save('README.md', '''# Growing fallback JIT lookup table

Commit `df8a7720` grows the fallback PC lookup table geometrically instead of
retaining 256 buckets for arbitrarily many live instructions. It follows the
physical build4448 profile that identified `ss_get` and its mutex paths as the
next startup bottleneck. All table readers and growth use the existing lock;
rehash repairs predecessor links and preserves page indexes, node addresses,
generation checks and deferred reclamation. Allocation failure retains the
old table with bounded retry frequency. The source and independent review
explain overflow handling and remaining hash-clustering limits.

The host actual-function harness passes 338,461 assertions with ASan/UBSan,
including concurrent hits during eight resizes, identity preservation,
invalidation/reinsertion, OOM recovery and overflow checks. Both the historical
invalidation benchmark and four-stride lookup benchmark pass. Host timings and
node counts are synthetic workload evidence, not game FPS predictions.

The rebuilt local CLI also contains the scheduler-query fix later committed
as `d8fa5c1e`. Its SHA256 is
`2f8a87fc674799138ee99226292528a8bb498fb22e768e0ab111f17e0073d007`.
Both runtime provenance records name that same CLI; the recorded JIT and
scheduler source hashes are verified against committed blobs in `d8fa5c1e`.
The records retain the then-current HEAD `df8a7720`, because the scheduler
change was still uncommitted when those controls were captured.

All four real ARM64 code-modification controls pass: ordinary and actual
MSR-FPCR fallback SMC, each in normal JIT and tier 2. Exact Mono initialization
also returns a nonnull domain and exit 0 with no SDL and full embedded SDL using
the local dummy display. These are local functional checks. No physical
build4449 result, rendered game frame, sound, playability or FPS is inferred.

`source/` contains exact `df8a7720` blobs and `source-change.patch` is the
committed change. `host/` preserves the earlier analysis/provenance verbatim;
its precommit status statements describe that earlier capture, while this
README and root provenance record the completed combined CLI controls.
`runtime-controls/` contains small own-process logs and Mono excerpts. Large
raw syscall logs, CLI binaries and proprietary Unity/Mono/game bytes are
excluded; omitted raw logs retain size/hash records. New app/device evidence
belongs in the separate build4449 archive.
''')
save('SHA256SUMS', ''.join(sha(p.read_bytes()) + '  ' + str(p.relative_to(OUT)) + '\n'
                         for p in sorted(OUT.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))
shutil.copytree(OUT, DEST, dirs_exist_ok=True)
for directory in [OUT, DEST]:
    for line in (directory / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1); assert sha((directory / name).read_bytes()) == digest
print(json.dumps({'files': sum(p.is_file() for p in OUT.rglob('*')),
                  'bytes': sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file()),
                  'sha256sums': sha((OUT / 'SHA256SUMS').read_bytes())}, indent=2))
