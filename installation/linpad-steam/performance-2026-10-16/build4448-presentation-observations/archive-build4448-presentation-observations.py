#!/usr/bin/env python3
"""Archive fixed later4448 observations without altering frozen release evidence."""
from pathlib import Path
import hashlib
import json
import shutil

PERF = Path(__file__).resolve().parent
WORK = PERF.parent
OUT = PERF / 'build4448-presentation-observations'
DEST = WORK / 'LinPad/installation/linpad-steam/performance-2026-10-16/build4448-presentation-observations'
OUT.mkdir(exist_ok=True)
records = []


def digest(data): return hashlib.sha256(data).hexdigest()


def save(name, data):
    if isinstance(data, str): data = data.encode()
    path = OUT / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)


def cp(source, name):
    data = source.read_bytes(); save(name, data)
    records.append({'source': str(source.relative_to(WORK.parent)), 'destination': name,
                    'sha256': digest(data), 'bytes': len(data), 'mode': 'byte-identical'})


def excerpt(source, name, predicate):
    data = source.read_bytes()
    text = '\n'.join(f'{i+1}: {line}' for i, line in enumerate(data.decode().splitlines()) if predicate(line)) + '\n'
    save(name, text)
    records.append({'source': str(source.relative_to(WORK.parent)), 'destination': name,
                    'raw_sha256': digest(data), 'raw_bytes': len(data), 'archived_sha256': digest(text.encode()),
                    'mode': 'numbered selected lines; complete raw report remains local'})


cp(Path(__file__), 'archive-build4448-presentation-observations.py')
cp(PERF / 'serve/hollow4448-compositor-status', 'diagnostics/hollow4448-compositor-status')
cp(PERF / 'reports/compositor4448-status.txt', 'compositor4448-status.txt')
cp(PERF / 'build4448/swapchain-profile-summary.json', 'profile/swapchain-profile-summary.json')
cp(PERF / 'jit-ss-growth/swapchain-lock-paths.json', 'profile/swapchain-lock-paths.json')
cp(PERF / 'jit-ss-index/summarize-time-profile.py', 'profile/summarize-time-profile.py')

markers = ('InitializeOrResetSwapChain', 'Vulkan API version', 'Vulkan renderer=',
           'Loaded All Assemblies', '[Physics::Module] Name:', 'UnloadTime:',
           'Steam initializing', 'Steam failed to initialize', 'Selected online subsystem',
           'SteamAPI_Init()', 'SteamAPI_IsSteamRunning()', 'steamclient.so',
           'FMOD failed', 'Unable to initalize any audio device', 'resizing window',
           'requesting resize')
for filename in ['hollow4448-view-status-1.txt', 'hollow4448-short-1.txt',
                 'hollow4448-short-2.txt', 'hollow4448-short-3.txt']:
    excerpt(PERF / 'reports' / filename, filename.replace('.txt', '-excerpt.txt'),
            lambda line: line.startswith(('Fri Oct', 'FRAME_LOG=', 'FRAME_LOG_ROWS=', 'FRAME_LINES',
                                          'HOLLOW_TITLE_MAP_CANDIDATES', 'HOLLOW_ACTIVE_BUFFER_CANDIDATES',
                                          'COMPOSITOR_MAPS', '0 /tmp/hollow4448-frames.txt')) or
                         any(marker in line for marker in markers))

save('README.md', '''# Later build4448 presentation observations

This fixed snapshot extends the separate runtime follow-up, without changing
the signed build4448 artifact/release proof. The actual game logs requested
960x540 swapchain initialization at 12:31:19.818 UTC, an unload milestone at
12:34:43.558 and Steam initialization at 12:36:15.479. These log messages show
continued startup progress; they do not establish a presented game frame.

The passive device report at 12:35:48 verifies the actual compositor identity:
PID 16 runs `ishwl --runtime-dir /tmp/ishwl -v`, stdout/stderr target
`/tmp/ishwl.log`, and both `/tmp/xdg-runtime/wayland-0` and `wayland-0-gpu`
exist. The frame log still has zero rows, no live `v*.buf` view buffers are
reported, and the compositor log has no mapped toplevel. The later 12:36:50
short report also has zero rows. No game-view frame is qualified here.

Source confirms these paths: `LinuxGUIBridge.swift` launches the compositor
with that runtime/log, while `wl-bridge/src/dmabuf.c` accepts the GPU socket
into the same server/display. There is no separate GPU compositor required
by this implementation. Native DesktopKit/Terminal windows do not necessarily
create guest Wayland views, so their absence from the map log is expected.

The selected-environment section is empty because this LinPad procfs has no
`/proc/PID/environ` entry (`fs/proc/pid.c` implements `cmdline`). It must not
be interpreted as proof that ISHWL_FRAMELOG or the other variables are absent.
The script prints only a small allowed set if that interface exists.

A second bounded CPU profile contains 9,435 samples, 9.435 seconds of aggregate
sample weight over about 5.436 seconds of sampled wall-clock span. Self weight
is 37.997% in `__psynch_mutexwait`, 33.302% in `ss_get` and 21.876% in
`__psynch_mutexdrop`. The selected call paths place most mutex weight under
`ss_get`, supporting investigation of its fixed PC-index lookup/lock cost.
Sample weights are not frame rate, GPU utilization, measured lock hold time
or proof of a deadlock. A subsequent source repair requires a new build.

Separate unresolved startup messages are retained: FMOD could not initialize
an output device, and SteamAPI reports no running Steam instance plus a
missing `/root/.steam/sdk64/steamclient.so`; Steam initialization fails at
12:36:18.827. This snapshot does not assign those messages as the cause of
absent frames and does not claim sound or Steam integration works.

Raw profiler captures, process inventories, complete logs and game binaries
remain local. `provenance.json` contains raw-source hashes and excerpt lineage.
The prepared compositor diagnostic reads regular files/limited metadata and
does not consume bridge FIFOs, send signals or change configuration. Frozen
build4448 release evidence and the earlier runtime-follow-up are unchanged.
''')
save('provenance.json', json.dumps({'build': '4448',
                                  'source_commit': 'd4ea14293dc2526e9a14de55f676adcdf88e4e31',
                                  'status': 'no compositor game frames observed at this snapshot',
                                  'files': records}, indent=2) + '\n')
save('SHA256SUMS', ''.join(digest(p.read_bytes()) + '  ' + str(p.relative_to(OUT)) + '\n'
                         for p in sorted(OUT.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))
shutil.copytree(OUT, DEST, dirs_exist_ok=True)
for directory in [OUT, DEST]:
    for line in (directory / 'SHA256SUMS').read_text().splitlines():
        sha, filename = line.split('  ', 1)
        assert digest((directory / filename).read_bytes()) == sha
print(json.dumps({'files': sum(p.is_file() for p in OUT.rglob('*')),
                  'bytes': sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file()),
                  'sha256sums': digest((OUT / 'SHA256SUMS').read_bytes())}, indent=2))
