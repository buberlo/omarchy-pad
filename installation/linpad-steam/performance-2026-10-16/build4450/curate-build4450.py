#!/usr/bin/env python3
"""Curate existing build4450 artifacts and completed reports; no device/network/Git mutations.

Does not rerun or overwrite record-artifact.py or its immutable initial manifest.
Large IPA/dSYM archives stay in this scratch build directory, never copied to Git.
"""
from pathlib import Path
import datetime,hashlib,importlib.util,json,plistlib,re,shutil,subprocess
import xml.etree.ElementTree as ET
BASE=Path(__file__).resolve().parent
WORK=BASE.parents[1]
REPO=WORK/'LinPad'
OUT=BASE/'curated'
DEST=REPO/'installation/linpad-steam/performance-2026-10-16/build4450'
OUT.mkdir(exist_ok=True)
records=[]
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def save(name,data):
 p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True)
 p.write_bytes(data if isinstance(data,bytes) else data.encode())
 return p
def cp(src,name=None):
 src=Path(src);name=name or src.name
 dst=save(name,src.read_bytes());records.append({'source':str(src.relative_to(WORK.parent)),'destination':name,'source_sha256':sha(src),'source_bytes':src.stat().st_size,'mode':'byte-identical','archived_sha256':sha(dst)})
 return dst
def excerpt(src,name,predicate):
 src=Path(src);data=src.read_bytes();lines=data.decode(errors='replace').splitlines()
 chosen=[f'{i+1}: {line}' for i,line in enumerate(lines) if predicate(line,i)]
 dst=save(name,'\n'.join(chosen)+'\n');records.append({'source':str(src.relative_to(WORK.parent)),'destination':name,'source_sha256':hashlib.sha256(data).hexdigest(),'source_bytes':len(data),'mode':'numbered selected lines; full raw source remains local','archived_sha256':sha(dst)})
 return '\n'.join(lines)
initial=json.loads((BASE/'build-manifest.json').read_text())
assert initial['build']=='4450' and initial['source_commit']==(BASE/'source-commit.txt').read_text().strip()
# Recheck small identity-bearing files; original record already verifies ZIP CRC and every member.
app=BASE/'Payload/iSH ARM64.app';info=plistlib.loads((app/'Info.plist').read_bytes());exe=app/info['CFBundleExecutable'];dwarf=BASE/'iSH ARM64.app.dSYM/Contents/Resources/DWARF'/info['CFBundleExecutable']
assert info['CFBundleVersion']=='4450'
assert sha(exe)==initial['executable_sha256'] and sha(dwarf)==initial['dwarf_sha256']
for row in initial['archives']:assert (BASE/row['name']).stat().st_size==row['bytes']
for row in initial['source_files']:
 p=BASE/'source-snapshot'/row['path'];assert sha(p)==row['sha256'];cp(p,'source-snapshot/'+row['path'])
 cp_blob=subprocess.check_output(['git','show',initial['source_commit']+':'+row['path']],cwd=REPO)
 assert hashlib.sha256(cp_blob).hexdigest()==row['sha256']
for n in ['source-commit.txt','source-changes.patch','build-local.sh','record-artifact.py','artifact-verification.log','signed-entitlements.plist']:
 cp(BASE/n)
cp(BASE/'build-manifest.json','initial-build-manifest.json')
cp(Path(__file__))
excerpt(BASE/'build.log','build-summary.log',lambda s,i:i<3 or '** BUILD SUCCEEDED **' in s or 'ISH_JIT_BUILD = enabled' in s or 'jit            : enabled' in s)
excerpt(BASE/'sign.log','signing-excerpt.txt',lambda s,i:s.startswith('SIGNED AND VERIFIED'))
physical={'installed':'pending','launched':'pending','native_jit_512MiB':'pending','boot_completed':'pending',
          'ordinary_smc':'pending','fallback_smc':'pending','game_rendered_frame':'pending',
          'gameplay':'pending','audio_playback':'pending','performance':'pending'}
install_file=BASE/'install.json'
if install_file.exists():
 install=json.loads(install_file.read_text())
 if install.get('info',{}).get('outcome')=='success':
  apps=install['result']['installedApplications']
  assert any(a['bundleID']==initial['bundle_id'] for a in apps)
  physical['installed']=True
  save('device-install-evidence.json',json.dumps({'outcome':'success','command_type':install['info']['commandType'],
       'installed_bundle_ids':[a['bundleID'] for a in apps],'raw_install_sha256':sha(install_file),
       'raw_install_bytes':install_file.stat().st_size,'raw_identifiers_and_container_paths_excluded':True},indent=2)+'\n')
console_file=BASE/'device-console.log'
if console_file.exists():
 console=excerpt(console_file,'device-boot-excerpt.txt',lambda s,i:s.startswith('Launched application with ') or '[boot]' in s or s.startswith('ish: native JIT on:'))
 if 'Launched application with '+initial['bundle_id'] in console: physical['launched']=True
 if 'ish: native JIT on:' in console and '512 MB code arena' in console: physical['native_jit_512MiB']=True
 if '[boot] boot_seconds=' in console: physical['boot_completed']=True
reports=WORK/'performance/reports';serve=WORK/'performance/serve'
cp(serve/'smc4450-probe','device-scripts/smc4450-probe')
p=reports/'smc4450-device-pass.txt'
if p.exists():
 text=p.read_text();assert len(text)<131072
 assert all(v in text for v in ['smc ok','SMC_DEVICE_EXIT=0','jit-ss-smc PASS checks=200 failures=0','SS_SMC_DEVICE_EXIT=0'])
 physical['ordinary_smc']={'status':'passed','rewrites':200,'exit':0,'source_sha256':sha(p),'evidence':p.name}
 physical['fallback_smc']={'status':'passed','checks':200,'failures':0,'exit':0,'source_sha256':sha(p),'evidence':p.name}
 excerpt(p,p.name,lambda s,i:s.startswith(('Fri Oct','smc ok','jit-ss-smc PASS','SMC_DEVICE_EXIT=',
     'SS_SMC_DEVICE_EXIT=','/tmp/smc-arm64:','/tmp/jit-ss-smc-probe:','FRAME_LOG_STARTED=')))
previous=reports/'finish4449-prepare4450.txt'
previous_text=previous.read_text()
assert all(v in previous_text for v in ['OPERATOR_SENT_SIGTERM=366','OPERATOR_SENT_SIGKILL=366','5633 /tmp/hollow4449-frames.txt'])
excerpt(previous,'predecessor4449-end-excerpt.txt',lambda s,i:s.startswith(('Fri Oct','OPERATOR_REASON=',
    'OPERATOR_SENT_SIGTERM=','OPERATOR_SENT_SIGKILL=','NEXT_BOOT_FRAME_CAPTURE_READY=','FRAME_ROWS',
    '5633 /tmp/hollow4449-frames.txt')) or 'InitializeOrResetSwapChain' in s or "Restored language code 'DE'" in s
    or 'Making UI menu lean.' in s or "map toplevel 1 'Hollow Knight'" in s)
predecessor={'build':'4449','operator_stop_utc':'2026-10-16T13:10:37Z/2026-10-16T13:10:40Z',
    'operator_sent_signals':['SIGTERM','SIGKILL'],'guest_pid':366,'total_frame_rows_observed':5633,
    'raw_report_sha256':sha(previous),'evidence':'predecessor4449-end-excerpt.txt',
    'scope':'Prior build menu/setup run manually stopped to install 4450; no normal gameplay qualification or aggregate FPS estimate.'}
# This title-menu observation belongs to this exact build. It is not gameplay.
image=WORK.parent/'outputs/hollow-knight-title-menu-4450.png'
cp(image,image.name)
cadence_report=reports/'hollow4450-cadence-title-menu.txt'
assert sha(cadence_report)=='da62df7f182c0520216f55d0fc69b55158aac48fca45c765a276f1755bfdd5e3'
cadence_text=cadence_report.read_text()
assert 'CAPTURE_EXIT=0' in cadence_text and 'FRAME_CAPTURE_REPORT_END' in cadence_text
cp(cadence_report)
for part in ['meta','txt','status.txt']:
 value=cadence_text.split('BEGIN_CAPTURE_'+part+'\n',1)[1].split('END_CAPTURE_'+part+'\n',1)[0]
 save('title-menu-window.'+part,value)
capture_status=(OUT/'title-menu-window.status.txt').read_text()
assert 'VIEW_BUFFER_CHANGED' not in capture_status and 'FRAME_LOG_TRUNCATED' not in capture_status
assert capture_status.count('/tmp/ishwl/v1-2.buf')==2
spec=importlib.util.spec_from_file_location('frames',WORK/'performance/parse-hollow-frame-window.py')
parser=importlib.util.module_from_spec(spec);spec.loader.exec_module(parser)
cadence=parser.analyze((OUT/'title-menu-window.txt').read_text(),(OUT/'title-menu-window.meta').read_text(),1)
assert cadence['frames_observed']==210 and cadence['captured_wall_seconds']==30
assert cadence['full_window_rate_approx_fps']==7.0 and not cadence['sequence_discontinuities']
cadence['qualification']='Static title menu. Parent verified the game foreground before and after the measured interval, with no window switch during it. Compositor delivery cadence only; no gameplay/GPU FPS or matched improvement claim.'
save('title-menu-cadence-analysis.json',json.dumps(cadence,indent=2)+'\n')
for src,dst in [
 ('serve/hollow4450-menu-cadence','device-scripts/hollow4450-menu-cadence'),
 ('serve/hollow4450-frame-capture','device-scripts/hollow4450-frame-capture'),
 ('parse-hollow-frame-window.py','diagnostics/parse-hollow-frame-window.py'),
 ('test-hollow-frame-parser.py','diagnostics/test-hollow-frame-parser.py'),
 ('frame-parser-test-results.json','diagnostics/parser-tests.json')]:
 cp(WORK/'performance'/src,dst)
excerpt(reports/'hollow4450-short-2.txt','game-startup-excerpt.txt',lambda s,i:s.startswith('Fri Oct') or
 any(v in s for v in ['Vulkan API version','Vulkan renderer=','Vulkan driverversion=',
 'Loaded All Assemblies','InitializeOrResetSwapChain',"saved language code 'DE'","Restored language code 'DE'",'Steam failed to initialize']))
physical['game_rendered_frame']=True
physical['audio_playback']={'status':'user-reported audible menu music',
 'reported_date':'2026-10-16',
 'quality':'User reports the menu music stutters badly; smooth or uninterrupted audio is not established.',
 'evidence_type':'Direct user report relayed by parent; no audio recording or quantitative audio test.'}
physical['visible_title_menu']={'status':'observed','first_observation_utc_approx':'2026-10-16T13:20:48Z',
 'screenshot':image.name,'screenshot_sha256':sha(image),
 'scope':'German title menu visible in unchanged physical-iPad mirror screenshot; observation time is not a first-frame latency measurement.'}
physical['bounded_title_menu_cadence']={'start_utc':'2026-10-16T13:21:03Z','end_utc':'2026-10-16T13:21:33Z',
 'view_id':1,'rows':210,'seconds':30,'composited_deliveries_per_second':7.0,
 'swapchain_extent':[659,540],'foreground':'Parent confirmed no window switch during the measured interval; menu visible by13:20:48 and screenshot checked13:21:34 UTC.',
 'pre_delay_seconds':{'operator_before_wrapper':15,'wrapper':5},
 'evidence':'title-menu-cadence-analysis.json','not_gameplay_fps':True}
# Stable independent aggregate audit. Raw trace samples and device-identifying
# TOC sections stay local; retain only exact timing fields from each TOC.
profile=WORK/'performance/build4450-profile-audit'
for name in ['startup-summary.json','startup-call-paths.json','loading-summary.json',
             'loading-call-paths.json','analyze-paths.py','provenance.json']:
 cp(profile/name,'profile/'+name)
cp(profile/'README.md','profile/source-audit.md')
timing={}
for stage in ['startup','loading']:
 toc=profile/(stage+'-toc.xml');root=ET.fromstring(toc.read_bytes())
 summary=root.find('.//summary');assert summary is not None
 timing[stage]={k:summary.findtext(k) for k in ['start-date','end-date','duration','end-reason','template-name','time-limit']}
 timing[stage]['raw_toc_sha256']=sha(toc)
 timing[stage]['excluded']='Device identifiers, container paths, process inventory and unrelated table descriptions.'
 summary_json=json.loads((profile/(stage+'-summary.json')).read_text())
 raw=BASE/(stage+'-profile-samples.xml')
 assert sha(raw)==summary_json['input_sha256']
save('profile/trace-timing.json',json.dumps(timing,indent=2)+'\n')
save('profile/README.md','''# Build 4450 CPU profile boundary

These stable aggregates analyze two already-recorded, bounded own-app profiles.
The startup interval is 13:14:45.059–13:14:50.844 UTC. The separate loading
interval is 13:17:31.201–13:17:37.094 UTC, before the title-menu observation.
The latter has 15,343 samples and 15.343 weighted CPU seconds across a
5.557-second sample span. Weighted CPU samples across threads are not wall time.

Loading still spends 5.103 weighted seconds (33.26%) in mutex wait/wake beneath
ss_get; its inclusive share is 40.28%. CPU-state copies surrounding gadget
fallback consume 13.26%. These categories overlap. The sample cannot tell
whether context-cache misses come from generation changes, collisions or the
working set. No unmeasured cause or gameplay speedup is asserted.

source-audit.md is the unchanged independent source analysis. Its references
to original TOCs describe local inputs: only their timing fields and hashes
are included as trace-timing.json here. Raw trace/XML samples, device metadata
and proprietary game bytes are excluded. provenance.json and aggregate files
retain the original input/source hashes. analyze-paths.py can recompute path
categories when given the matching local exported sample XML.

This loading profile is separate from the later 30-second static-menu
compositor cadence sample. It is not a profile of that menu interval.
''')
manifest=dict(initial)
manifest['curated_at_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
manifest['curation_frozen']=True
manifest['physical']=physical
manifest['predecessor_run']=predecessor
manifest['device_validation']='Frozen checkpoint. Build 4450 physical boot/SMC and visible title menu qualify; bounded foreground compositor cadence is recorded. User reports audible but badly stuttering menu music. Normal gameplay and playable performance remain pending.'
manifest['initial_build_manifest_sha256']=sha(BASE/'build-manifest.json')
manifest['scope']='Large IPA/dSYM archives stay outside Git. No raw process/console inventories, device identifiers, provisioning-profile bytes or proprietary game inputs are copied.'
save('build-manifest.json',json.dumps(manifest,indent=2)+'\n')
save('symbols.json',json.dumps({'executable_sha256':initial['executable_sha256'],'dwarf_sha256':initial['dwarf_sha256'],
    'matching_uuid':initial['app_and_dsym_uuid'],'architecture':'arm64'},indent=2)+'\n')
archive_sums=''.join(r['sha256']+'  '+r['name']+'\n' for r in initial['archives'])
save('SHA256SUMS',archive_sums);(BASE/'SHA256SUMS').write_text(archive_sums)
boot_text='Installed and booted on the physical iPad with 512 MiB native JIT' if all(physical[k] is True for k in ['installed','launched','native_jit_512MiB','boot_completed']) else 'Physical install/launch/JIT/boot qualification is pending or incomplete; see individual manifest fields'
smc_text='Both physical ordinary and actual fallback self-modification tests pass 200 rewrites' if physical['fallback_smc']!='pending' else 'Physical ordinary and fallback self-modification tests remain pending'
notes=f'''LinPad 1.4.0 build 4450 adds a 256-entry persistent fallback-instruction cache per JIT context to reduce repeated shared lookup locking. App source is `{initial['source_commit']}`. Documentation was committed separately as 0afc2925 and is not the app source pin.

The signed IPA and matching dSYM passed the original recorder's full ZIP CRC and bundle-content checks, with matching UUID `{initial['app_and_dsym_uuid']}`. Both approved memory entitlements remain enabled. {boot_text}. {smc_text}.

Independent source review passed. The actual-function fixture passes 438495 ASan/UBSan assertions, including stale-pointer generation rejection and one-shot exclusion. Unchanged real local ordinary/fallback SMC and exact default-FEX Mono noSDL/fullSDLdummy controls pass; their source hashes and CLI identity are archived separately in `../jit-ss-context-cache`. Synthetic lock counts and local timings are not physical game performance.

The preceding build 4449 run was manually sent SIGTERM then SIGKILL at 13:10:37–40 UTC to install this candidate. It had 5633 aggregate compositor rows and reached language/title-menu/setup stages; this is not a gameplay or aggregate FPS result. Its unchanged screenshots and qualified cadence samples remain in `../build4449-visible-menu`. None are attributed to build 4450.

The new build displays the German Hollow Knight title menu, first observed around 13:20:48 UTC after the 13:13:59 launch. The unchanged screenshot is included. This observation time is not a measured first-frame latency. Unity requested 960×540 initially, then reset the swapchain to 659×540 at 13:15:54.300; the bounded capture retained the same view 1 and backing-file identity at that size.

From 13:21:03 to 13:21:33 UTC, the static title-menu view delivered 210 compositor rows over the full 30 seconds: **7.0 composited deliveries/second**. Sequences 3183–3392 are contiguous, median interval 142 ms, 95th percentile 149 ms, maximum 184 ms. The parent confirmed the menu foreground before capture and checked the screenshot at 13:21:34, with no window switch during the measured interval. An operator-added 15-second delay preceded the wrapper's recorded five-second delay. This measures game-view delivery through the compositor, not GPU time, independent display refresh or gameplay FPS. The report and parser preserve the full-window count, separate from its active-span rate.

On 2026-10-16 the user reports audible menu music that stutters badly. This is user-reported evidence, without an audio recording or instrumented playback measurement. Normal gameplay and steady-state playable performance remain pending.

The earlier 13:17:31–37 loading profile still attributes 33.26% of weighted CPU samples to mutex wait/wake under shared SS lookup, and 13.26% to fallback CPU-state copying. Its exact aggregates and source analysis are included under `profile`. This is a different phase from the menu capture and does not establish why cache misses occur.

This checkpoint is frozen. Installed FEX/Mesa/EGL/Steam/game data are separate from the IPA. Prior 4449 scheduler/helper results are retained in that build's archive and are not relabeled as 4450 retests. The saved DE language preference carries over from 4449, and its prior menu capture had uncertain foreground state; neither startup nor menu cadence supports a matched causal performance comparison.
'''
save('release-notes.md',notes);(BASE/'release-notes.md').write_text(notes)
verification=f'''# Build 4450 verification

{notes}
The immutable original artifact record is copied as initial-build-manifest.json. Its recorder was not modified or rerun. This curator rechecks executable/DWARF hashes and exact source snapshots against committed blobs; it preserves the original archive verification log and digest/size records without claiming a second full ZIP traversal.

SHA256SUMS covers the large release assets kept outside Git. REPOSITORY-SHA256SUMS covers this lightweight folder. source-evidence.json fingerprints original logs and identifies narrowed excerpts. The predecessor 4449 evidence is explicitly separate from the physical 4450 gates. No release upload/digest verification is claimed here.

The parent owns further device qualification and publication. Subsequent gameplay or source work belongs in a separate follow-up record. Later opt-in diagnostics are separate source work and are not attributed to this build.
'''
save('VERIFICATION.md',verification)
save('source-evidence.json',json.dumps({'files':records},indent=2)+'\n')
save('REPOSITORY-SHA256SUMS',''.join(sha(f)+'  '+str(f.relative_to(OUT))+'\n' for f in sorted(OUT.rglob('*')) if f.is_file() and f.name!='REPOSITORY-SHA256SUMS'))
shutil.copytree(OUT,DEST,dirs_exist_ok=True)
for root in [OUT,DEST]:
 for line in (root/'REPOSITORY-SHA256SUMS').read_text().splitlines():
  h,n=line.split('  ',1);assert sha(root/n)==h,n
 for f in root.rglob('*.json'):json.loads(f.read_text())
print(json.dumps({'curation_frozen':True,'curated_files':sum(f.is_file() for f in OUT.rglob('*')),
    'repo_hash':sha(OUT/'REPOSITORY-SHA256SUMS'),'physical':physical},indent=2))
