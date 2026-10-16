#!/usr/bin/env python3
"""Freeze first visible menu, accepted language input and bounded loading cadence."""
from pathlib import Path
import hashlib
import importlib.util
import json
import shutil

PERF=Path(__file__).resolve().parent
WORK=PERF.parent
OUT=PERF/'build4449-visible-menu'
DEST=WORK/'LinPad/installation/linpad-steam/performance-2026-10-16/build4449-visible-menu'
OUT.mkdir(exist_ok=True)
records=[]
def sha(data): return hashlib.sha256(data).hexdigest()
def save(name,data):
    if isinstance(data,str): data=data.encode()
    p=OUT/name; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(data)
def cp(src,name):
    data=src.read_bytes(); save(name,data)
    records.append({'source':str(src.relative_to(WORK.parent)), 'destination':name,
                    'sha256':sha(data),'bytes':len(data),'mode':'byte-identical'})

image=WORK.parent/'outputs/hollow-knight-first-frame-4449.png'
assert sha(image.read_bytes())=='2c2890d845a56391511327ca442b3b5e8faeaafefd7c9fe3f1a1f545fcedd9d5'
cp(image,image.name)
title_image=WORK.parent/'outputs/hollow-knight-title-menu-4449.png'
cp(title_image,title_image.name)
cp(Path(__file__),Path(__file__).name)
report=PERF/'reports/hollow4449-cadence-loading.txt'
raw=report.read_bytes(); text=raw.decode()
assert sha(raw)=='9fdb3350a2601397cc1fd862c68ee340e38f7a5300bfac6bdffffb9d2ab4f3ae'
assert 'CAPTURE_EXIT=0' in text
cp(report,'hollow4449-cadence-loading.txt')
for part in ['meta','status.txt','txt']:
    value=text.split('BEGIN_CAPTURE_'+part+'\n',1)[1].split('END_CAPTURE_'+part+'\n',1)[0]
    save('loading-window.'+part,value)
status=(OUT/'loading-window.status.txt').read_text()
assert 'VIEW_BUFFER_CHANGED' not in status and 'FRAME_LOG_TRUNCATED' not in status
assert status.count('/tmp/ishwl/v1-2.buf')==2
spec=importlib.util.spec_from_file_location('frames',PERF/'parse-hollow-frame-window.py')
parser=importlib.util.module_from_spec(spec);spec.loader.exec_module(parser)
analysis=parser.analyze((OUT/'loading-window.txt').read_text(),(OUT/'loading-window.meta').read_text(),1)
assert analysis['frames_observed']==48 and analysis['captured_wall_seconds']==30
assert analysis['full_window_rate_approx_fps']==1.6 and not analysis['sequence_discontinuities']
save('loading-cadence-analysis.json',json.dumps(analysis,indent=2)+'\n')
title_report=PERF/'reports/hollow4449-cadence-title-menu.txt'
title_raw=title_report.read_bytes(); title_text=title_raw.decode()
assert sha(title_raw)=='52c386fa6db7529cc190237d297f0a5372d84e24c8f8d2672e0916edb4420a70'
assert 'CAPTURE_EXIT=0' in title_text and 'hollow4449-cadence-20261016T130638Z-IdPpHn' in title_text
cp(title_report,'hollow4449-cadence-title-menu.txt')
for part in ['meta','status.txt','txt']:
    value=title_text.split('BEGIN_CAPTURE_'+part+'\n',1)[1].split('END_CAPTURE_'+part+'\n',1)[0]
    save('title-menu-window.'+part,value)
title_analysis=parser.analyze((OUT/'title-menu-window.txt').read_text(),(OUT/'title-menu-window.meta').read_text(),1)
assert title_analysis['frames_observed']==97 and title_analysis['captured_wall_seconds']==30
title_analysis['qualification']='Title menu context; foreground uncertain/partial. Parent observed the terminal foreground initially and confirmed the game foreground around 13:07 UTC. Not a controlled foreground baseline or gameplay FPS.'
save('title-menu-cadence-analysis.json',json.dumps(title_analysis,indent=2)+'\n')

input_report=PERF/'reports/hollow4449-short-3.txt'
data=input_report.read_bytes(); lines=data.decode().splitlines()
needles=('InitializeOrResetSwapChain',"language code 'DE'",'Performing automatic level start.',
         'Game controller set to None.', 'map toplevel')
excerpt='\n'.join(f'{i+1}: {s}' for i,s in enumerate(lines) if s.startswith('Fri Oct') or any(n in s for n in needles))+'\n'
assert "2026-10-16T12:56:44.040Z" in excerpt and '659x540' in excerpt
save('input-and-resize-excerpt.txt',excerpt)
records.append({'source':str(input_report.relative_to(WORK.parent)), 'destination':'input-and-resize-excerpt.txt',
    'raw_sha256':sha(data),'raw_bytes':len(data),'archived_sha256':sha(excerpt.encode()),
    'mode':'numbered input/language/resize/map milestones; raw process and game logs remain local'})
for source,name in [
    ('serve/hollow4449-menu-cadence','diagnostics/serve/hollow4449-menu-cadence'),
    ('serve/hollow4449-frame-capture','diagnostics/serve/hollow4449-frame-capture'),
    ('parse-hollow-frame-window.py','diagnostics/parse-hollow-frame-window.py'),
    ('test-hollow-frame-parser.py','diagnostics/test-hollow-frame-parser.py'),
    ('frame-parser-test-results.json','diagnostics/parser-tests.json'),
    ('test-menu-cadence-wrapper.py','diagnostics/test-menu-cadence-wrapper.py'),
    ('menu-cadence-wrapper-tests.json','diagnostics/wrapper-tests.json')]:
    cp(PERF/source,name)

save('first-visible-provenance.json',json.dumps({
    'build':'4449','source_commit':'d8fa5c1ed8bbe324677408ae8768a2f243cde412',
    'image_sha256':sha(image.read_bytes()),'image_transformation':'none; byte-identical screenshot',
    'title_image_sha256':sha(title_image.read_bytes()),'title_image_observation_utc':'2026-10-16T13:05:00Z',
    'observation_utc':'2026-10-16T12:54:00Z','observation_precision':'minute only; first captured observation, not exact first-frame timestamp',
    'visual_review':'Hollow Knight window displays a language list and ARE YOU SURE? YES/NO confirmation on the mirrored physical iPad.',
    'input_evidence':'Parent reports accepting Deutsch; Unity subsequently logs restored DE at12:56:44 and13:01:35.',
    'capture_scene':'Parent observed black transition after accepted Deutsch; no gameplay scene.',
    'second_capture_scene':'Title menu context, but foreground uncertain/partial from 13:06:43 to 13:07:13 UTC; unsuitable as a controlled foreground baseline.',
    'gate_status':{'visible_menu':True,'language_selection_input_response':True,
        'bounded_loading_compositor_cadence_recorded':True,'normal_gameplay':'pending',
        'audio_playback':'not qualified by this archive','steady_state_playable_performance':'pending'},
},indent=2)+'\n')
save('README.md','''# Build4449 visible menus, input and qualified cadence observations

The unchanged screenshot establishes a visible Hollow Knight language list
and confirmation dialog on the mirrored physical iPad. Parent observed it at
approximately12:54UTC after the12:47:41 launch. This is the first captured
observation, not a measured first-frame latency. The game was not restarted
or instrumented with the unfinished Vulkan proxy.

Parent accepted Deutsch in the game. Unity logs restoration of DE at
12:56:44.040 and again13:01:35.577; this confirms language-selection input
response. It does not establish movement/combat or normal gameplay. Subsequent
observation was a black loading transition. The log's automatic-level-start
message is retained without claiming a visible playable level.

The passive capture waited five seconds for foregrounding, then observed
13:04:02–13:04:32UTC. Exact titled view1 retained the same v1-2.buf identity.
It delivered48 compositor frame rows over the full30seconds: **1.6 composited
frames/second**. All48 sequences are contiguous,4436–4483, with no other view
rows. The47 inter-frame intervals span15.663seconds (active-only3.00/s); that
shorter-span rate must not replace the full-window result. Median interval is
297ms,95th percentile389ms,maximum1766ms. The parser flags the long interval;
the captured compositor-event slice contains no acknowledgement warning.

The run initially requested960×540. Unity later logged a swapchain reset to
659×540 at12:51:16.853; the observed capture damage is also659×540 and the
unchanged backing file is1,423,440bytes (659×540×4). Damage dimensions alone
would not prove swapchain size; here the separate Unity message provides that
evidence. This resized black transition is not comparable to a960×540 stable
gameplay scene.

The measured boundary is the compositor's game-view output, not GPU render
duration or independently measured display refresh. The snapshot confirms
visible menu content and a working language input path, while the transition
remains very slow. Normal gameplay, audio playback and steady-state playable
performance remain unqualified here.

A second unchanged screenshot, observed around 13:05 UTC, shows the German
main title menu with Spiel starten, Optionen, Erfolge, Extras and Spiel verlassen.
The second 30-second capture runs from 13:06:43 to 13:07:13 UTC and contains
97 rows for the same view: 3.23 compositor frames/second over the full window.
Median interval is 304 ms, 95th percentile 327 ms and maximum 436 ms; sequences
4908–5004 are contiguous. The active-only rate is 3.25/s over 29.517 seconds.
**Foreground state was uncertain/partial:** parent saw the terminal foreground
initially and confirmed the game foreground around 13:07. This is a title-menu
context observation, not a controlled foreground baseline, improvement over
the loading transition or gameplay FPS. The different scenes and foreground
states prevent a matched performance comparison between these captures.

The report, exact meta/rows/status and parser are included. The wrapper makes
no game/configuration/input changes and reads only regular files. It uses a
unique capture directory and one bounded POST to the parent's existing local
collector. Five wrapper tests use synthetic files and stub waits/network;
five parser tests exercise arithmetic. They are separate from the physical
capture. Reproduce analysis with:

```
python3 diagnostics/parse-hollow-frame-window.py loading-window.txt loading-window.meta --view1
```

Raw process inventories, full game logs and proprietary game binaries remain
outside Git. The original screenshot bytes are preserved, including the
mirroring application's surrounding UI, in this private evidence archive.
Frozen build4449 signing/probe proof and the earlier pre-frame snapshot remain
unchanged. Subsequent source f93f1449 belongs to a future build and is not
attributed to this physical result.
'''.replace('approximately12:54UTC','approximately 12:54 UTC').replace('the12:47:41','the 12:47:41').replace('again13:01','again 13:01').replace('13:04:32UTC','13:04:32 UTC').replace('view1','view 1').replace('delivered48','delivered 48').replace('full30seconds','full 30 seconds').replace('All48','All 48').replace('contiguous,4436','contiguous, 4436').replace('The47','The 47').replace('span15.663seconds','span 15.663 seconds').replace('active-only3.00','active-only 3.00').replace('297ms,95th percentile389ms,maximum1766ms','297 ms, 95th percentile 389 ms, maximum 1766 ms').replace('requested960','requested 960').replace('at12:51','at 12:51').replace('also659','also 659').replace('is1,423,440bytes','is 1,423,440 bytes').replace('a960','a 960').replace('--view 1','--view 1'))
save('provenance.json',json.dumps({'scope':'Visible menus and accepted language input; black-transition cadence plus title-menu cadence with uncertain/partial foreground; no gameplay FPS claim.',
    'snapshot_end_utc':'2026-10-16T13:07:13Z','files':records},indent=2)+'\n')
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
