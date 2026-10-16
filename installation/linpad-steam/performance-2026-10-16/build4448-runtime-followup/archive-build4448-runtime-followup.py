#!/usr/bin/env python3
"""Curate a fixed build4448 runtime snapshot separately from release proof."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import xml.etree.ElementTree as ET

PERF = Path(__file__).resolve().parent
WORK = PERF.parent
OUT = PERF / 'build4448-runtime-followup'
DEST = WORK / 'LinPad/installation/linpad-steam/performance-2026-10-16/build4448-runtime-followup'
OUT.mkdir(exist_ok=True)
records = []


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save(name, data):
    if isinstance(data, str): data = data.encode()
    p = OUT / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(data)
    return p


def cp(source, name):
    source = Path(source); data = source.read_bytes(); save(name, data)
    records.append({'source': str(source.relative_to(WORK.parent)), 'destination': name,
                    'sha256': sha(data), 'bytes': len(data), 'mode': 'byte-identical'})


def excerpt(source, name, predicate):
    data = source.read_bytes()
    text = '\n'.join(f'{i+1}: {line}' for i, line in enumerate(data.decode().splitlines()) if predicate(line)) + '\n'
    save(name, text)
    records.append({'source': str(source.relative_to(WORK.parent)), 'destination': name,
                    'raw_sha256': sha(data), 'raw_bytes': len(data), 'archived_sha256': sha(text.encode()),
                    'mode': 'numbered selected milestone lines; raw remains local'})


cp(Path(__file__), 'archive-build4448-runtime-followup.py')
cp(PERF / 'jit-ss-index/summarize-time-profile.py', 'profile/summarize-time-profile.py')
subprocess.run(['python3', str(PERF / 'jit-ss-index/summarize-time-profile.py'),
                str(PERF / 'build4448/startup-profile-samples.xml'), '--out',
                str(OUT / 'profile/summary.json')], check=True, stdout=subprocess.DEVNULL)
summary = json.loads((OUT / 'profile/summary.json').read_text())
assert summary['top_self'][0]['function'] == 'ss_get'
assert summary['top_self'][0]['weighted_seconds'] == 4.612
toc = ET.fromstring((PERF / 'build4448/startup-profile-toc.xml').read_bytes())
capture = toc.find('run/info/summary')
save('profile/capture-metadata.json', json.dumps({
    'app_build': '4448', 'source_commit': 'd4ea14293dc2526e9a14de55f676adcdf88e4e31',
    'matching_app_dsym_uuid': 'A5CFFE3B-C626-3F4A-A0CB-50CEC07154EB',
    'start': capture.findtext('start-date'), 'end': capture.findtext('end-date'),
    'duration': capture.findtext('duration'),
    'raw_toc_sha256': sha((PERF / 'build4448/startup-profile-toc.xml').read_bytes()),
    'raw_export_sha256': summary['input_sha256'],
    'raw_trace_and_identifiers_archived': False,
    'metric': 'statistical native CPU sample weight, not GPU utilization or game FPS',
}, indent=2) + '\n')

milestones = ('The selected window backend', 'Display 0 ', 'Desktop is', 'Vulkan API version',
              'Vulkan renderer=', 'Vulkan driverversion=', 'Vulkan driverLibraryString=',
              'Begin MonoManager ReloadAssembly', 'Loaded All Assemblies', '[Physics::Module] Name:',
              '[Physics::Module] Threading Mode:', 'Vulkan PSO:')
for i in [1, 2, 3]:
    excerpt(PERF / f'reports/hollow4448-status-{i}.txt', f'progress/status-{i}-milestones.txt',
            lambda line: line.startswith('Fri Oct') or any(word in line for word in milestones))
excerpt(PERF / 'reports/hollow4447-manual-stop-game-output.txt', 'progress/build4447-comparison-milestones.txt',
        lambda line: line.startswith('Fri Oct') or 'Vulkan API version' in line or 'Vulkan renderer=' in line)

for source, dest in [
    ('serve/hollow4448-frame-capture', 'diagnostics/serve/hollow4448-frame-capture'),
    ('test-hollow4448-frame-capture.py', 'diagnostics/test-hollow4448-frame-capture.py'),
    ('hollow4448-frame-capture-tests.json', 'diagnostics/capture-tests.json'),
    ('parse-hollow-frame-window.py', 'diagnostics/parse-hollow-frame-window.py'),
    ('test-hollow-frame-parser.py', 'diagnostics/test-hollow-frame-parser.py'),
    ('frame-parser-test-results.json', 'diagnostics/parser-tests.json'),
]:
    cp(PERF / source, dest)

save('FRAME-CAPTURE.md', '''# Prepared per-game frame capture

This diagnostic has not been run on the iPad. Build4448's physical SMC report
confirms that `/tmp/hollow4448-frames.txt` was created by the compositor.
Only the parent task operates the device. The script reads ordinary files;
it does not consume `events`/`notify` FIFOs or change/restart the game.

After downloading the prepared `hollow4448-frame-capture` into `/tmp`, inspect:

```
sh /tmp/hollow4448-frame-capture status
```

It lists the exact `map toplevel N 'Hollow Knight'` records and current
`vN-GENERATION.buf` files, plus recent Unity graphics milestones. The compositor
unlinks these buffer files when a view unmaps or resizes. A historical map
without a buffer is not accepted as a live capture candidate. Multiple active
candidates require an explicit observed view ID; the script refuses to guess.

Once a physical screenshot confirms actual game content in a stable scene,
start the fixed thirty-second sample (or supply an observed numeric ID):

```
sh /tmp/hollow4448-frame-capture capture auto 30
```

The script allows 1–60 seconds and refuses an existing output prefix. It saves:

- `/tmp/hollow4448-frame-window.meta`: view ID, wall-clock seconds and line boundaries.
- `/tmp/hollow4448-frame-window.txt`: only frame-log rows appended in that window.
- `/tmp/hollow4448-frame-window.status.txt`: title/buffer identity, milestone context and new acknowledgement warnings.

Transfer those regular files through the existing parent-owned collection
method. On the Mac, read `view_id` from the metadata and run:

```
python3 work/performance/parse-hollow-frame-window.py \\
  work/performance/reports/hollow4448-frame-window.txt \\
  work/performance/reports/hollow4448-frame-window.meta --view OBSERVED_VIEW_ID
```

For another sample set `LINPAD_FRAME_CAPTURE=/tmp/hollow4448-scene2` before
invoking the script; the script never overwrites an earlier result. Its other
optional path overrides are `LINPAD_FRAME_LOG`, `LINPAD_COMPOSITOR_LOG`,
`LINPAD_VIEW_RUNTIME`, and `LINPAD_UNITY_LOG`. Defaults match build4448.

Report the full-window selected-view row rate, including idle time. The
active-span rate is separate and must not replace it. The parser handles
32-bit timestamp/sequence wrap, excludes other views and flags discontinuities.
`VIEW_BUFFER_CHANGED`, map replacement, acknowledgement warnings or a resize
require qualification. Damage dimensions are not the swapchain extent.
This metric is composited game-view delivery cadence, not GPU render time
or independently measured physical display FPS. Physical scene/start/end
captures and an input-response check are still necessary for gameplay claims.

Seven synthetic fake-file checks cover view identification, bounded sampling,
overwrite prevention, duration limit and rejection of ambiguous/stale IDs.
The separate five parser checks cover idle spans, wrap, gaps and malformed
data. These are local arithmetic/diagnostic checks, not device performance.
''')
save('README.md', '''# Build4448 runtime follow-up snapshot

This is separate from the frozen build4448 release/artifact proof. The actual
game launched at 12:28:50 UTC and logged Vulkan API initialization at
12:29:03.958 (about 14 seconds), loaded Mono assemblies at 12:29:35.456 and
selected PhysX at 12:29:36.794. The previous build4447 run logged the same
Vulkan API milestone at 12:05:49.625 after its 11:57:56 start (about 474 seconds).
These are individual observed startup stages, not a matched repeated benchmark,
time-to-first-frame measurement or steady-state speedup claim. Cache/thermal
state was not experimentally controlled.

The new bounded own-app CPU capture has 5,518 samples / 5.518 seconds of sample
weight. `ss_get` now accounts for 4.612 seconds (83.58%) of self weight. There
are no self samples in `ss_invalidate_locked` in this capture. The profiles
were taken at different startup stages; the result identifies the next cache
lookup bottleneck and does not by itself prove overall game performance.

No rendered game frame or playability was confirmed at this snapshot. The
prepared frame collector and host parser are in `diagnostics/`; exact usage
and interpretation are in `FRAME-CAPTURE.md`. These diagnostic tools were
checked locally, not executed on the device by the archiving agent.

Raw traces, device identifiers, process inventories, proprietary game binaries
and complete game logs are excluded. Numbered milestone excerpts retain source
hashes in `provenance.json`; the reproducible profiler summarizer is included.
Build4448 source remains `d4ea1429`. Any subsequent cache-lookup repair must
have its own source/build evidence and must not be retroactively attributed
to this already-signed app.
''')
save('provenance.json', json.dumps({'scope': 'fixed runtime snapshot after physical SMC pass, game frame still pending',
                                  'build': '4448', 'release_artifacts_unchanged': True,
                                  'files': records}, indent=2) + '\n')
save('SHA256SUMS', ''.join(sha(p.read_bytes()) + '  ' + str(p.relative_to(OUT)) + '\n'
                         for p in sorted(OUT.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))
shutil.copytree(OUT, DEST, dirs_exist_ok=True)
for directory in [OUT, DEST]:
    for line in (directory / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1)
        assert sha((directory / name).read_bytes()) == digest
print(json.dumps({'files': sum(p.is_file() for p in OUT.rglob('*')),
                  'bytes': sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file()),
                  'checksum_sha256': sha((OUT / 'SHA256SUMS').read_bytes())}, indent=2))
