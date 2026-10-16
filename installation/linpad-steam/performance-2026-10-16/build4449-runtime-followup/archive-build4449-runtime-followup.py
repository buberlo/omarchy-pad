#!/usr/bin/env python3
"""Curate a fixed runtime snapshot, separately from frozen build4449 proof."""
from pathlib import Path
import hashlib
import json
import shutil
import xml.etree.ElementTree as ET

PERF = Path(__file__).resolve().parent
WORK = PERF.parent
OUT = PERF / 'build4449-runtime-followup'
DEST = WORK / 'LinPad/installation/linpad-steam/performance-2026-10-16/build4449-runtime-followup'
OUT.mkdir(exist_ok=True)
records = []
def sha(data): return hashlib.sha256(data).hexdigest()
def save(name, data):
    if isinstance(data, str): data = data.encode()
    p = OUT / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)
    return p
def cp(source, name):
    data = source.read_bytes()
    save(name, data)
    records.append({'source': str(source.relative_to(WORK.parent)), 'destination': name,
                    'sha256': sha(data), 'bytes': len(data), 'mode': 'byte-identical'})
def excerpt(source, name, predicate):
    data = source.read_bytes()
    text = '\n'.join(f'{i+1}: {line}' for i, line in enumerate(data.decode().splitlines()) if predicate(line)) + '\n'
    save(name, text)
    records.append({'source': str(source.relative_to(WORK.parent)), 'destination': name,
                    'raw_sha256': sha(data), 'raw_bytes': len(data), 'archived_sha256': sha(text.encode()),
                    'mode': 'numbered selected lines; raw process and game logs remain local'})

cp(Path(__file__), Path(__file__).name)
cp(PERF/'jit-ss-index/summarize-time-profile.py', 'profile/summarize-time-profile.py')
cp(PERF/'build4449/startup-profile-summary.json', 'profile/summary.json')
summary = json.loads((OUT/'profile/summary.json').read_text())
assert summary['samples'] == 6202
assert next(x for x in summary['top_self'] if x['function'] == 'ss_get')['weighted_seconds'] == 0.133
assert sha((PERF/'build4449/startup-profile-samples.xml').read_bytes()) == summary['input_sha256']
toc_bytes = (PERF/'build4449/startup-profile-toc.xml').read_bytes()
capture = ET.fromstring(toc_bytes).find('run/info/summary')
save('profile/capture-metadata.json', json.dumps({
    'build': '4449', 'source_commit': 'd8fa5c1ed8bbe324677408ae8768a2f243cde412',
    'matching_app_dsym_uuid': 'AB1037BA-59E4-36E9-B2EB-A55908E809CB',
    'start': capture.findtext('start-date'), 'end': capture.findtext('end-date'),
    'duration': capture.findtext('duration'), 'raw_toc_sha256': sha(toc_bytes),
    'raw_export_sha256': summary['input_sha256'],
    'raw_trace_and_device_identifiers_archived': False,
    'metric': 'statistical native CPU sample weight; not GPU use, lock duration or FPS',
}, indent=2)+'\n')

needles = ('The selected window backend', 'Display 0 ', 'Desktop is', 'Vulkan API version',
           'Vulkan renderer=', 'Vulkan driver', 'Begin MonoManager ReloadAssembly',
           'Loaded All Assemblies', '[Physics::Module] Name:', 'InitializeOrResetSwapChain',
           'requesting resize', 'resizing window', 'UnloadTime:', 'Steam initializing',
           'Steam failed to initialize', 'SteamAPI_', 'steamclient.so',
           "Couldn't find a Game Manager", "Couldn't find GameCameras", "Couldn't find a UIManager",
           'Discovered supported languages', 'Loaded saved language', 'Restored language',
           'Unloading 5 Unused', 'Unloading 49 unused', 'Total: 386.944000')
for base in ['hollow4449-status-1.txt', 'hollow4449-short-1.txt', 'hollow4449-short-2.txt']:
    excerpt(PERF/'reports'/base, 'progress/'+base,
            lambda s: s.startswith('Fri Oct') or any(n in s for n in needles)
            or s in ('FRAME_LINES', 'COMPOSITOR_MAPS') or s.strip() == '0 /tmp/hollow4449-frames.txt')
for base in ['hollow4449-frame-capture', 'hollow4449-compositor-status']:
    cp(PERF/'serve'/base, 'diagnostics/'+base)

save('README.md', '''# Build4449 runtime follow-up snapshot

This snapshot is separate from the frozen signed-build/probe checkpoint. The
game launch record is 12:47:41 UTC. Vulkan API initialization is logged at
12:47:47.661 (about 6.7 seconds after launch). Mono assembly reload spans
12:47:48.632–12:47:54.098 and reports 5.466 seconds; this is the reload duration,
not the elapsed time since game launch. Unity enters InitializeOrResetSwapChain
for 960×540 at 12:48:07.963, about 27 seconds after launch. These are individual
startup observations, not a controlled repeated benchmark or time to first frame.

The bounded CPU capture runs from 12:48:35.823 to 12:48:41.613 UTC, during the
Steam initialization part of startup. Across 6,202 samples / 6.202 seconds of
sample weight, ss_get self weight is 0.133 seconds (2.14%). The preceding
build4448 startup capture had 83.58% ss_get self weight and its later capture
had 33.30%. Different startup phases and cache/thermal conditions prevent an
overall speedup estimate. The new largest individual self entry is memmove
at 15.82%; no further implementation change is justified by this summary alone.

Managed startup continues through unload, language restoration and object
cleanup. Steam initialization fails because sdk64/steamclient.so is missing.
The log also reports missing Game Manager, GameCameras and UIManager objects.
These messages are retained as observations without asserting that any one
causes the missing image. No FMOD initialization error appears in the captured
startup report; that is not an audio-playback qualification.

At 12:49:25 and 12:50:25 UTC the passive report finds zero frame-log rows and
no compositor map records. No rendered game image or gameplay is established.
InitializeOrResetSwapChain is a Unity stage message, not evidence that
vkCreateSwapchainKHR returned success, vkQueuePresentKHR was called, or a
nonempty buffer reached the compositor. The next diagnostic should distinguish
those boundaries before changing graphics settings.

The two prepared passive scripts use build4449 paths and do not restart the
game. Their collector/parser ancestry and synthetic validation are archived in
build4448-runtime-followup. No fixed-duration selected-view capture was possible
at this snapshot because no game view was identified. Raw traces, device IDs,
process inventories, proprietary inputs and full logs remain outside Git.
''')
save('PRESENTATION-NEXT.md', '''# Narrow next presentation observation

Current compositor observables are passive: check the known ishwl log and
frame file, then identify a map with the exact game title and a live matching
vN-generation.buf file. Do not consume bridge events/notify FIFOs. The game
can keep running during these checks. Native Terminal/DesktopKit content does
not imply a Linux Wayland map record exists.

The source path is wl-bridge/src/xdg.c (map state after a committed nonempty
surface buffer) and view.c composite_view (frame row after composition and
before bridge delivery/acknowledgement). Zero rows alone cannot distinguish
no image submission, invalid geometry, pending callbacks or absent logging.
Combining the known log start with no game map narrows the boundary but still
does not identify a Vulkan call or return value.

If the same launch continues without an image, a separate diagnostic game
launch can observe vkCreateSwapchainKHR, vkAcquireNextImageKHR/2KHR,
vkQueueSubmit/2/KHR, vkQueuePresentKHR and waits. Record first entry and return,
VkResult and device/queue/swapchain identity, then bounded periodic counts.
Keep entry and return separate to expose a call that never returns. Report
successful presents as API progress only; they do not prove display delivery.

Do not reuse the earlier vulkan-present-timing.c cube proxy. It has global
single-device/queue state, reports only at exit and was not qualified for Unity.
A new candidate needs real-loader global dispatch trampolines or proper per-
device dispatch, synchronization, pass-through of extension availability,
multi-device/queue tests, direct and get-proc-address lookup coverage, and a
bounded log. Keep it in a separate optional loader directory and never replace
the base Vulkan loader or production launcher. Test locally before any physical
use, then use only the parent-owned device workflow.

A next-run WAYLAND_DEBUG=client capture is a lower-implementation alternative
to locate wl_surface attach/commit/frame events and responses. It cannot by
itself distinguish time spent before Vulkan present or inside GPU/fence waits,
and protocol logging changes runtime overhead. No restart or new hook was
performed by the archiving agent.
''')
save('provenance.json', json.dumps({'build': '4449', 'release_artifacts_unchanged': True,
    'snapshot_end_utc': '2026-10-16T12:50:25Z', 'game_frame_qualified': False,
    'gameplay_qualified': False, 'performance_qualified': False, 'files': records}, indent=2)+'\n')
save('SHA256SUMS', ''.join(sha(p.read_bytes())+'  '+str(p.relative_to(OUT))+'\n'
    for p in sorted(OUT.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))
shutil.copytree(OUT, DEST, dirs_exist_ok=True)
for directory in [OUT, DEST]:
    for line in (directory/'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1)
        assert sha((directory/name).read_bytes()) == digest
print(json.dumps({'files': sum(p.is_file() for p in OUT.rglob('*')),
    'bytes': sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file()),
    'checksum_sha256': sha((OUT/'SHA256SUMS').read_bytes())}, indent=2))
