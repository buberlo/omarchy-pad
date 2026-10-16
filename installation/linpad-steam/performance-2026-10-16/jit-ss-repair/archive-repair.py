#!/usr/bin/env python3
"""Assemble narrow JIT repair evidence; no device, network or Git mutations."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
WORK = HERE.parents[1]
REPO = WORK / 'LinPad'
OUT = WORK / 'performance/jit-ss-repair'
DEST = REPO / 'installation/linpad-steam/performance-2026-10-16/jit-ss-repair'
FIX = 'd4ea14293dc2526e9a14de55f676adcdf88e4e31'
TEST = subprocess.check_output(['git', 'rev-parse', 'e334e1b1'], cwd=REPO, text=True).strip()
OUT.mkdir(exist_ok=True)
records = []


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save(name, data):
    if isinstance(data, str):
        data = data.encode()
    p = OUT / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)
    return p


def cp(source, name):
    source = Path(source)
    data = source.read_bytes()
    save(name, data)
    records.append({'source': str(source.relative_to(WORK.parent)),
                    'destination': name, 'sha256': sha(data), 'bytes': len(data),
                    'mode': 'byte-identical'})


for name in ['analysis.md', 'summarize-time-profile.py', 'profile-summary.json',
             'baseline-provenance.json', 'provenance.json', 'regression.txt',
             'benchmark.txt', 'jit-ss-index-harness.c']:
    cp(HERE / name, 'host/' + name)
cp(Path(__file__), 'archive-repair.py')

for commit, paths in [(FIX, ['jit/jit.c', 'jit/jit_internal.h',
                            'tests/manual/steam-compat/test-jit-ss-index.py']),
                      (TEST, ['tests/manual/steam-compat/jit-ss-smc-probe.c']),
                      (FIX, ['tests/arm64-insn/smc.c'])]:
    for name in paths:
        data = subprocess.check_output(['git', 'show', commit + ':' + name], cwd=REPO)
        save('source/' + name, data)
        records.append({'source_commit': commit, 'source_path': name,
                        'destination': 'source/' + name, 'sha256': sha(data),
                        'bytes': len(data), 'mode': 'exact committed blob'})

for commit, name in [(FIX, 'production-fix.patch'), (TEST, 'fallback-smc-test.patch')]:
    data = subprocess.check_output(['git', 'show', '--format=fuller', '--no-ext-diff', commit], cwd=REPO)
    save(name, data)

review = WORK / 'performance/ss-invalidation-review'
for name in ['README.md', 'reviewed-source.json', 'smc-results.json',
             'smc-jit.txt', 'smc-tier2.txt', 'fallback-smc-results.json',
             'fallback-smc-jit.txt', 'fallback-smc-tier2.txt']:
    cp(review / name, 'guest-controls/' + name)
assert 'smc ok' in (OUT / 'guest-controls/smc-jit.txt').read_text()
assert 'smc ok' in (OUT / 'guest-controls/smc-tier2.txt').read_text()
for mode in ['jit', 'tier2']:
    text = (OUT / f'guest-controls/fallback-smc-{mode}.txt').read_text()
    assert 'jit-ss-smc PASS checks=200 failures=0' in text
    assert '201 fallbacks, 200 invalidations' in text
    assert '200 dead single-step blocks' in text

binaries = []
for name in ['smc-arm64', 'jit-ss-smc-probe']:
    p = WORK / 'performance/serve' / name
    binaries.append({'name': name, 'bytes': p.stat().st_size,
                     'sha256': sha(p.read_bytes()), 'binary_archived': False})
profile = json.loads((HERE / 'profile-summary.json').read_text())
assert profile['samples'] == 5592
assert profile['top_self'][0]['function'] == 'ss_invalidate_locked'
assert profile['top_self'][0]['weighted_seconds'] == 5.346
manifest = {
    'production_commit': FIX, 'guest_fallback_test_commit': TEST,
    'target_app_build': '4448',
    'before_physical_profile': {
        'app_build': '4447', 'app_dsym_uuid': 'DEDA2762-5AAF-3685-9513-27727BEF64C6',
        'requested_capture_seconds': 5, 'samples': profile['samples'],
        'sample_weight_seconds': profile['sample_weight_seconds'],
        'self_ss_invalidate_weight_seconds': 5.346,
        'self_ss_invalidate_percent': profile['top_self'][0]['percent'],
        'raw_export_sha256': profile['input_sha256'],
        'raw_trace_archived': False,
        'scope': 'Own-app native CPU sampling during actual Hollow Knight startup; no frame/FPS conclusion.'},
    'host_regression': {'assertions': 240144, 'asan_ubsan': 'passed',
                        'production_functions_extracted': True,
                        'constructor_failure_cleanup': 'source-reviewed, not fault-injected'},
    'real_guest_smc': {'ordinary': '200 rewrites pass in JIT and tier2',
                       'fallback': '200 rewrites pass in JIT and tier2; 201 fallbacks and 200 invalidations/retired SS entries'},
    'guest_binary_fingerprints': binaries,
    'physical_4448': 'Recorded separately in ../build4448; not inferred from these local controls.',
    'gameplay_and_performance': 'pending physical qualification',
    'files': records,
}
save('provenance.json', json.dumps(manifest, indent=2) + '\n')
save('README.md', '''# JIT single-step cache invalidation repair

Physical build 4447's bounded Time Profiler capture found 95.60% of sampled
CPU weight in a whole-cache invalidation scan. The fix indexes fallback
instructions by guest page and preserves exact page matching, locked updates
and deferred reclamation. Source commit: `d4ea1429`; target app build: 4448.

The host harness runs actual extracted production functions and passes
240,144 assertions with ASan/UBSan. Its 1,000 false-positive page writes visit
1,000 nodes with the new index versus 65,536,000 with the old scan. Synthetic
microbenchmark timing is not a game-startup or FPS prediction.

Real ARM64 execution checks pass 200 code rewrites in normal JIT and tier 2.
The dedicated `MSR FPCR` fallback probe also passes both modes; JIT counters
confirm 201 fallbacks, 200 invalidations and 200 retired single-step entries.
That test source is a separate commit, `e334e1b1`, after the app's production
source pin. It was not compiled into the app.

`source/` contains exact committed source blobs, with their commit/hash records
in `provenance.json`. `production-fix.patch` is the committed change.
`host/` contains the test source generated from the production functions,
sanitizer/benchmark results and replayable native-profile summarizer.
`guest-controls/` contains the narrow local CLI checks and independent review.
Guest binary hashes are recorded; binaries and raw Instruments traces remain
local. No proprietary game libraries, game data, device IDs or raw process
inventories are included.

For the host test, run the canonical script from the LinPad checkout:

```
python3 tests/manual/steam-compat/test-jit-ss-index.py --out /tmp/linpad-ss-index
```

See `host/analysis.md` for invariants and limitations. New physical build4448
gates are maintained separately in `../build4448`. These local checks do not
establish a rendered game frame, playability or steady-state performance.
''')
save('SHA256SUMS', ''.join(sha(p.read_bytes()) + '  ' + str(p.relative_to(OUT)) + '\n'
                         for p in sorted(OUT.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))
shutil.copytree(OUT, DEST, dirs_exist_ok=True)
for directory in [OUT, DEST]:
    for line in (directory / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1)
        assert sha((directory / name).read_bytes()) == digest, name
print(json.dumps({'files': sum(p.is_file() for p in OUT.rglob('*')),
                  'bytes': sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file()),
                  'checksums_sha256': sha((OUT / 'SHA256SUMS').read_bytes())}, indent=2))
