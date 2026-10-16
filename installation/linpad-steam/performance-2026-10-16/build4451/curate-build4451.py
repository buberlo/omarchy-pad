#!/usr/bin/env python3
"""Curate existing build4451 artifacts and completed reports; no device/network/Git mutations.

Does not rerun or overwrite record-artifact.py or its immutable initial manifest.
Large IPA/dSYM archives stay in this scratch build directory, never copied to Git.
"""
from pathlib import Path
import datetime,hashlib,importlib.util,json,plistlib,re,shutil,subprocess,sys
assert sys.argv[1:] in [[],["--freeze"]]
FREEZE="--freeze" in sys.argv
import xml.etree.ElementTree as ET
BASE=Path(__file__).resolve().parent
WORK=BASE.parents[1]
REPO=WORK/'LinPad'
OUT=BASE/'curated'
DEST=REPO/'installation/linpad-steam/performance-2026-10-16/build4451'
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
assert initial['build']=='4451' and initial['source_commit']==(BASE/'source-commit.txt').read_text().strip()
# Recheck small identity-bearing files; original record already verifies ZIP CRC and every member.
app=BASE/'Payload/iSH ARM64.app';info=plistlib.loads((app/'Info.plist').read_bytes());exe=app/info['CFBundleExecutable'];dwarf=BASE/'iSH ARM64.app.dSYM/Contents/Resources/DWARF'/info['CFBundleExecutable']
assert info['CFBundleVersion']=='4451'
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
excerpt(BASE/'signing.log','signing-excerpt.txt',lambda s,i:s.startswith('SIGNED AND VERIFIED'))
physical={k:'pending' for k in ['installed','launched','native_jit_512MiB','boot_completed',
 'ordinary_smc','fallback_smc','native_diagnostic_counters','game_diagnostic_counters',
 'game_rendered_frame','gameplay','audio_playback','performance']}
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
 if 'Launched application with '+initial['bundle_id'] in console:physical['launched']=True
 if 'ish: native JIT on:' in console and '512 MB code arena' in console:physical['native_jit_512MiB']=True
 if '[boot] boot_seconds=' in console:physical['boot_completed']=True
reports=WORK/'performance/reports';serve=WORK/'performance/serve'
for name in ['smc4451-probe','ss4451-probe']:
 cp(serve/name,'device-scripts/'+name)
for src,dst in [('test-ss4451-collector.py','diagnostics/test-ss4451-collector.py'),
 ('jit-ss-diagnostics-review/collector-tests.json','diagnostics/collector-tests.json')]:
 cp(WORK/'performance'/src,dst)
p=reports/'smc4451-device-pass.txt'
if p.exists():
 text=p.read_text();assert len(text)<131072
 assert all(v in text for v in ['smc ok','SMC_DEVICE_EXIT=0','jit-ss-smc PASS checks=200 failures=0','SS_SMC_DEVICE_EXIT=0'])
 physical['ordinary_smc']={'status':'passed','rewrites':200,'exit':0,'source_sha256':sha(p),'evidence':p.name}
 physical['fallback_smc']={'status':'passed','checks':200,'failures':0,'exit':0,'source_sha256':sha(p),'evidence':p.name}
 excerpt(p,p.name,lambda s,i:s.startswith(('Fri Oct','smc ok','jit-ss-smc PASS','SMC_DEVICE_EXIT=',
  'SS_SMC_DEVICE_EXIT=','/tmp/smc-arm64:','/tmp/jit-ss-smc-probe:','FRAME_LOG_STARTED=')))
p=reports/'ss4451-selftest-device-pass.txt'
if p.exists():
 text=p.read_text()
 assert all(v in text for v in ['SS_DIAG_PROC PASS enabled=1','SS_DIAG_DEVICE_EXIT=0',
  'hit=199998 miss_cold=2','path_negative=100000 path_exit=100000','truncated 0'])
 excerpt(p,'native-counter-selftest-excerpt.txt',lambda s,i:s.startswith(('Fri Oct',
  'supported ','enabled ','version ','approximate_counters ','counters_count_attempts ',
  'sample_window ','sample_probability_','sample_ring_size ','contexts_visited ','truncated ',
  'context_allocation_failed ','context tid=288 ','SS_DIAG_DEVICE_EXIT=')) or 'SS_DIAG_PROC PASS' in s)
 physical['native_diagnostic_counters']={'status':'passed','source_sha256':sha(p),
  'evidence':'native-counter-selftest-excerpt.txt','guest_pid':288,'attempts':200000,
  'hits':199998,'cold_misses':2,'persistent_fills':2,'samples':174,'negative_path_attempts':100000,
  'native_exit_path_attempts':100000,'truncated':False,'scope':'Native physical probe; not the game workload.'}
game_report=reports/'ss4451-hollow-startup-1.txt'
if game_report.exists():
 text=game_report.read_text();assert len(text)<950000
 assert all(v in text for v in ['BUILD=4451','GAME_PID=348','MAIN_DELTA_ELIGIBLE=1',
  'ALL_DELTA_GATES_VALID=1','PROCESS_CONTINUITY_ELIGIBLE=1','MAPS_TRUNCATED=0'])
 main_rows=[]
 for line in text.splitlines():
  if line.startswith('context tid=348 '):
   main_rows.append({k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)})
 assert len(main_rows)==2
 assert all(main_rows[0][k]==main_rows[1][k] for k in ['tid','tgid','mmu','reset'])
 assert main_rows[0]['tid']==348 and main_rows[0]['tgid']==348
 delta_line=next(s for s in text.splitlines() if s.startswith('MAIN_DELTA '))
 delta={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',delta_line)}
 for k,v in delta.items():
  if k not in ['tid','mmu','reset']:
   assert main_rows[1][k]-main_rows[0][k]==v and v>=0,(k,v)
 attempts=sum(delta[k] for k in ['hit','miss_cold','miss_collision','miss_generation'])
 misses=attempts-delta['hit']
 excerpt(game_report,'game-counter-startup-excerpt.txt',lambda s,i:s.startswith((
  'SS_DIAGNOSTIC_REPORT_','BUILD=','GAME_PID=','MUTATIONS=','COUNTERS=',
  'WORKER_DELTAS=','SNAPSHOT_BEGIN=','SNAPSHOT_END=','Fri Oct','IDENTITY ',
  'READ_EXIT=','RAW_BYTES=','supported ','enabled ','version ','approximate_counters ',
  'counters_count_attempts ','sample_window ','sample_probability_','path_legend ',
  'miss_legend ','sample_ring_size ','contexts_visited ','truncated ',
  'context_allocation_failed ','FILTER_','SNAPSHOT_ELIGIBLE=','context tid=348 ',
  'PROCESS_CONTINUITY_ELIGIBLE=','MAIN_DELTA','MAPS_TRUNCATED=',
  'MAPS_READ_EXIT=','ALL_DELTA_GATES_VALID=')))
 summary={'source_sha256':sha(game_report),'guest_pid':348,
  'interval_utc':['2026-10-16T13:46:02Z','2026-10-16T13:46:12Z'],
  'main_thread_identity':{k:main_rows[0][k] for k in ['tid','tgid','mmu','reset']},
  'main_delta':delta,'main_attempts':attempts,'main_misses':misses,
  'hit_percent':100*delta['hit']/attempts,
  'collision_percent_of_misses':100*delta['miss_collision']/misses,
  'shared_hit_percent_of_misses':100*delta['shared_hit']/misses,
  'all_collector_gates_valid':True,'maps_truncated':False,
  'scope':'Approximate main-thread fallback attempts in a startup interval, not FPS or all-game CPU time. Worker deltas are not computed. Recent opcode rings cover a different, bounded recent sample window.'}
 save('game-counter-startup-summary.json',json.dumps(summary,indent=2)+'\n')
 physical['game_diagnostic_counters']={'status':'passed','guest_pid':348,
  'evidence':'game-counter-startup-excerpt.txt','summary':'game-counter-startup-summary.json',
  'source_sha256':sha(game_report),'main_attempts':attempts,
  'scope':'Actual game startup counter collection. Rendering is qualified separately by the later menu capture; these counters alone establish no audio, gameplay or performance acceptance.'}
# Baseline app qualification; the separate feature-mask experiment is excluded.
control=WORK/'performance/build4451-lrcpc2-control'
cadence_report=reports/'hollow4451-baseline-menu-cadence.txt'
if cadence_report.exists():
 text=cadence_report.read_text()
 assert 'CAPTURE_EXIT=0' in text and 'VIEW_BUFFER_CHANGED=1' not in text and 'FRAME_LOG_TRUNCATED=1' not in text
 cp(cadence_report)
 parts={}
 for part in ['meta','status.txt','txt']:
  begin='BEGIN_CAPTURE_'+part+'\n';end='END_CAPTURE_'+part+'\n'
  assert text.count(begin)==1 and text.count(end)==1
  parts[part]=text.split(begin,1)[1].split(end,1)[0]
  save('baseline-window.'+part,parts[part])
 meta=dict(x.split('=',1) for x in parts['meta'].split());view=int(meta['view_id'])
 assert view==1 and int(meta['end_second'])-int(meta['start_second'])==30
 spec=importlib.util.spec_from_file_location('frame_parser',WORK/'performance/parse-hollow-frame-window.py')
 parser=importlib.util.module_from_spec(spec);spec.loader.exec_module(parser)
 cadence=parser.analyze(parts['txt'],parts['meta'],view)
 assert cadence['frames_observed']==212 and not cadence['warnings']
 observed=json.loads((control/'baseline-menu-analysis.json').read_text())
 assert all(observed[k]==v for k,v in json.loads(json.dumps(cadence)).items())
 cp(control/'baseline-menu-analysis.json','baseline-menu-analysis.json')
 cp(control/'baseline-menu-provenance.json','baseline-menu-provenance.json')
 for name in ['hollow-knight-title-menu-4451-baseline-measurement.png','hollow-knight-title-menu-4451-baseline-end.png']:
  cp(WORK.parent/'outputs'/name)
 for name in ['hollow4451-menu-cadence','hollow4451-frame-capture','hollow4451-egl']:
  cp(serve/name,'device-scripts/'+name)
 for name in ['parse-hollow-frame-window.py','test-hollow-frame-parser.py']:
  cp(WORK/'performance'/name,'diagnostics/'+name)
 cp(WORK/'performance/frame-parser-test-results.json','diagnostics/frame-parser-test-results.json')
 physical['game_rendered_frame']={'status':'passed','scene':'German title menu','swapchain_extent':[659,540],
  'evidence':['hollow-knight-title-menu-4451-baseline-measurement.png','hollow-knight-title-menu-4451-baseline-end.png'],
  'qualification':'Both screenshots independently inspected; no normal gameplay acceptance.'}
 physical['performance']={'status':'bounded_menu_observation_only','interval_utc':['2026-10-16T13:55:24Z','2026-10-16T13:55:54Z'],
  'view_id':1,'frames_observed':212,'wall_seconds':30,'composited_deliveries_per_second':212/30,
  'foreground':'Parent verified before and after, with no intervening switch or input.',
  'evidence':'baseline-menu-analysis.json','playable_performance':'pending','gpu_fps_claim':False}
 for name in ['README.md','startup-summary.json','provenance.json','analyze-report.py','sampled-opcodes-disassembly.txt','opcode-counts.txt']:
  cp(WORK/'performance/build4451-ss-analysis'/name,'opcode-analysis/'+name)
 audit=json.loads((WORK/'performance/build4451-ss-analysis/provenance.json').read_text())
 checked=[]
 for path,h in audit['source_sha256'].items():
  if path.startswith('work/LinPad/'):
   rel=path.removeprefix('work/LinPad/')
   blob=subprocess.check_output(['git','show',initial['source_commit']+':'+rel],cwd=REPO)
   assert hashlib.sha256(blob).hexdigest()==h
   checked.append({'path':rel,'sha256':h,'matches_app_source':True})
 save('opcode-analysis/app-source-check.json',json.dumps({'app_source_commit':initial['source_commit'],
  'analysis_checkout_commit':audit['source_commit'],'meaning':'The analysis checkout includes later commits. All listed LinPad production blobs are independently identical to the app source commit. FEX is a separate guest binary/source audit.',
  'checked':checked},indent=2)+'\n')
end=reports/'finish4451-baseline.txt'
if end.exists():
 text=end.read_text();assert 'OPERATOR_SENT_SIGTERM=348' in text and 'OPERATOR_SENT_SIGKILL=348' in text
 excerpt(end,'baseline-stop-excerpt.txt',lambda s,i:s.startswith(('Fri Oct','OPERATOR_REASON=',
  'GAME_PID=','OPERATOR_SENT_SIGTERM=','OPERATOR_SENT_SIGKILL=')) or 'InitializeOrResetSwapChain' in s)
 physical['baseline_end']={'guest_pid':348,'operator_stop_utc':'2026-10-16T13:56:50Z/2026-10-16T13:56:53Z',
  'signals':['SIGTERM','SIGKILL'],'evidence':'baseline-stop-excerpt.txt','source_sha256':sha(end),
  'scope':'Menu baseline manually stopped for a separate feature-mask experiment; no spontaneous baseline crash claimed.'}
if FREEZE:
 assert physical['game_diagnostic_counters']['status']=='passed'
 assert physical['game_rendered_frame']['status']=='passed'
 assert physical['performance']['status']=='bounded_menu_observation_only'
previous=reports/'finish4450-prepare4451.txt';text=previous.read_text()
assert 'OPERATOR_SENT_SIGTERM=271' in text and 'OPERATOR_SENT_SIGKILL=271' in text
excerpt(previous,'predecessor4450-end-excerpt.txt',lambda s,i:s.startswith(('Fri Oct',
 'OPERATOR_REASON=','OPERATOR_SENT_SIGTERM=','OPERATOR_SENT_SIGKILL=','NEXT_BOOT_FRAME_CAPTURE_READY='))
 or bool(re.match(r'\s*\d+ /tmp/hollow4450-frames.txt$',s)) or 'InitializeOrResetSwapChain' in s)
count=re.search(r'(\d+) /tmp/hollow4450-frames.txt',text)
predecessor={'build':'4450','operator_stop_utc':'2026-10-16T13:41:09Z/2026-10-16T13:41:12Z',
 'operator_sent_signals':['SIGTERM','SIGKILL'],'guest_pid':271,
 'total_frame_rows_observed':int(count[1]) if count else None,'raw_report_sha256':sha(previous),
 'evidence':'predecessor4450-end-excerpt.txt',
 'scope':'Parent observed menu and new-slot2 introduction only; no normal gameplay qualification. Manually stopped for this diagnostic build. Aggregate rows are not FPS.'}
manifest=dict(initial)
manifest['curated_at_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
manifest['curation_frozen']=FREEZE
manifest['physical']=physical
manifest['predecessor_run']=predecessor
manifest['initial_build_manifest_sha256']=sha(BASE/'build-manifest.json')
manifest['device_validation']=('Frozen' if FREEZE else 'Unfrozen')+' baseline diagnostic checkpoint: boot/SMC/native and actual-game counters, visible German menu and bounded foreground compositor cadence qualify. Audio, normal gameplay and playable performance remain pending. The separate disablelrcpc2 experiment failed before Unity startup and is not the baseline configuration.'
manifest['scope']='Large release assets, raw process/maps/env data, device identifiers, provisioning-profile bytes and proprietary game inputs stay outside Git.'
save('build-manifest.json',json.dumps(manifest,indent=2)+'\n')
save('symbols.json',json.dumps({'executable_sha256':initial['executable_sha256'],
 'dwarf_sha256':initial['dwarf_sha256'],'matching_uuid':initial['app_and_dsym_uuid'],'architecture':'arm64'},indent=2)+'\n')
archive_sums=''.join(r['sha256']+'  '+r['name']+'\n' for r in initial['archives'])
save('SHA256SUMS',archive_sums);(BASE/'SHA256SUMS').write_text(archive_sums)
notes=f'''LinPad 1.4.0 build 4451 adds opt-in SS-cache diagnostics. App source is {initial['source_commit']}, based directly on {initial['source_base']}. Source records contain the six exact code/test blobs from that commit. This build adds observation; it does not implement a new instruction or promise a speedup.

The original recorder verified signature, both approved memory entitlements, full ZIP CRC/bundle contents and matching app/dSYM UUID {initial['app_and_dsym_uuid']}. The root filesystem is unchanged from 4450. Guest FEX, Mesa, EGL, Steam and game data are separate from the IPA.

This record is {'FROZEN' if FREEZE else 'UNFROZEN pending final checkpoint review'}. Read individual physical gates in build-manifest.json. Physical boot with 512 MiB JIT and both 200-rewrite SMC probes pass. The native counter self-test passes 200000 attempts: 199998 hits, two cold fills, 174 recent samples and 100000 attempts from each negative/native-exit path. It is not the game workload.

A separate actual-game startup capture at 13:46:02–12 UTC passed all collector gates for main guest thread 348, with identical tid/tgid/mmu/reset and one tick of allowed start-time drift. The main delta contains 140699252 approximate fallback attempts: 85.24% local cache hits, and 97.68% of misses caused by cache collisions. The retained excerpt excludes all maps and worker/sample rows; the raw report hash is preserved. These counters describe startup fallback attempts, not frame rate or gameplay acceptance. Detailed opcode analysis is maintained separately.

The baseline game reached the German title menu. The verified foreground capture at 13:55:24–54 UTC records 212 deliveries over 30 seconds, or **7.0667 composited game-view deliveries/s**, for view 1 at actual 659×540. Sequences 4718–4929 are contiguous; p50 interval is 140 ms, p95 150 ms and maximum 207 ms. The buffer identity was unchanged and the compositor reports no selected-view events. The parent confirmed foreground before and after with no intervening switch/input, and both saved screenshots independently show the title menu. This is menu cadence, not GPU FPS, physical refresh timing or normal gameplay performance.

All 1,223 retained recent opcode observations, including 128 main-thread records, match the LRCPC2 LDAPUR/STLUR family. None matches MSR FPCR. The rings may repeat worker records and are not a whole-interval frequency histogram. Compact analysis/disassembly is included; raw maps and full sample JSON remain local. The analysis checkout commit differs from the app commit because of later commits; app-source-check.json verifies every listed LinPad production blob against the signed app source.

The baseline was manually stopped with TERM then KILL at 13:56:50–53 UTC. A separate same-build launcher adding FEX_HOSTFEATURES=disablelrcpc2 failed pre-Unity at 13:57:22 with guest exit 139 and produced no candidate cadence. That failed control is preserved in ../build4451-lrcpc2-control and is not a recommended launch configuration. Its failure does not replace the baseline menu evidence. No normal gameplay or clean-audio acceptance is claimed.

Host option ISH_JIT_SS_DIAG=1 enables approximate per-context fallback-attempt counters and a bounded recent opcode-sample ring at /proc/ish/jit-ss. The default is disabled. A bounded atomic snapshot is copied under the registry lifetime lock and formatted after unlocking. There is no signal-handler hook or shared JIT-memory lock. Independent review, sanitizer and local real-proc evidence are archived in ../jit-ss-diagnostics.

The collector checks exact TGID/sample identity, incomplete snapshots and bounded process continuity with two-tick tolerance. Main-thread deltas additionally require identical tid/tgid/mmu/reset and nondecreasing exactly representable counters. Worker deltas are not computed because fields cannot prove an incarnation after TID reuse. Maps are a capped 200 KB prefix with explicit truncation. Invalid gates preserve/post labeled evidence then exit 70; reports above 950 KB stay local and are not posted. Eight synthetic tests stub process data, waits and network; they establish no device performance.

Build 4450 was manually sent SIGTERM then SIGKILL at 13:41:09–12 UTC after menu/new-slot2 introduction only. Its previously qualified menu cadence and user-reported stuttering music remain in the frozen 4450 record and are not attributed to 4451. Instrumentation adds observer overhead, so diagnostic timing is not a production performance comparison.
'''
save('release-notes.md',notes);(BASE/'release-notes.md').write_text(notes)
save('VERIFICATION.md','# Build 4451 verification ('+('frozen' if FREEZE else 'unfrozen')+')\n\n'+notes+'''
The initial immutable manifest and recorder are preserved. This curator rechecks executable/DWARF hashes and source snapshots against committed blobs and preserves the original archive verification; it does not claim a second full ZIP traversal. The earlier failed documentation-scope recording attempt remains local as artifact-verification-documentation-scope.log and does not replace the successful six-code/test-blob record.

SHA256SUMS covers large release assets outside Git. REPOSITORY-SHA256SUMS covers this lightweight folder. No private-release upload verification is claimed here. Subsequent game/performance evidence remains separate until explicitly qualified.
''')
save('source-evidence.json',json.dumps({'files':records},indent=2)+'\n')
save('REPOSITORY-SHA256SUMS',''.join(sha(f)+'  '+str(f.relative_to(OUT))+'\n' for f in sorted(OUT.rglob('*'))
 if f.is_file() and f.name!='REPOSITORY-SHA256SUMS' and '__pycache__' not in f.parts))
shutil.copytree(OUT,DEST,dirs_exist_ok=True)
for root in [OUT,DEST]:
 for line in (root/'REPOSITORY-SHA256SUMS').read_text().splitlines():
  h,n=line.split('  ',1);assert sha(root/n)==h,n
 for f in root.rglob('*.json'):json.loads(f.read_text())
print(json.dumps({'curation_frozen':FREEZE,'curated_files':sum(f.is_file() for f in OUT.rglob('*')),
 'repo_hash':sha(OUT/'REPOSITORY-SHA256SUMS'),'physical':physical},indent=2))
