#!/usr/bin/env python3
"""Curate the completed, manually stopped build4447 run only. No device, network, or Git mutations."""
from pathlib import Path
import datetime,hashlib,json,re,shutil
OUT=Path(__file__).resolve().parent
PERF=OUT.parent
WORK=PERF.parent
REPO=WORK/'LinPad/installation/linpad-steam/performance-2026-10-16/runtime-followup'
records=[]
def h(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def cp(src,dst):
 src=Path(src);dst=OUT/dst;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
 records.append({'source':str(src.relative_to(WORK.parent)),'destination':str(dst.relative_to(OUT)),'source_sha256':h(src),'archived_sha256':h(dst),'mode':'byte-identical'})
for n in ['README.md','probe.c','local-excerpt.txt','provenance.json']:
 cp(PERF/'cpu-accounting-audit'/n,'cpu-accounting/'+n)
for n in ['limit8-launch.c','after-maps-mono-limit8-results.json','after-maps-mono-limit8-provenance.json','after-maps-mono-limit8-0-excerpt.txt','after-maps-mono-limit8-3221-excerpt.txt']:
 cp(PERF/'mono-init-probe'/n,'stack-limit8/'+n)
for n in ['hollow-graphics-qualification.md','parse-hollow-frame-window.py','frame-parser-test-results.json','test-hollow-frame-parser.py']:
 if (PERF/n).exists():cp(PERF/n,'graphics/'+n)
for n in ['hollow4447-egl','hollow4447-status','hollow4447-detail','hollow4447-short','finish4447-prepare4448']:
 cp(PERF/'serve'/n,'launch-and-capture/'+n)
REPORTS=['hollow4447-status-1.txt','hollow4447-status-2.txt','hollow4447-status-3.txt','hollow4447-detail-1.txt','hollow4447-short-1.txt','hollow4447-short-2.txt','hollow4447-short-3.txt','hollow4447-short-4.txt','hollow4447-short-5.txt','hollow4447-manual-stop-game-output.txt','finish4447-prepare4448.txt']
timeline=[]
for name in REPORTS:
 src=PERF/'reports'/name;data=src.read_bytes();lines=data.decode(errors='replace').splitlines();selected=[]
 for i,s in enumerate(lines):
  phase=s.startswith('2026-10-16T') and ('extensions: name=' not in s or 'VK_KHR_swapchain ' in s)
  anchor='r-x' in s and any(x in s for x in ['/opt/fex/FEX-apple-m1-munmap','/UnityPlayer.so','/libmonobdwgc-2.0.so','/libEGL_mesa.so','/libgallium-','/libLLVM.so','/libvulkan_virtio.so'])
  keep=i==0 or bool(re.match(r'^Fri Oct 16 \d{2}:\d{2}:\d{2} UTC 2026$',s)) or s == 'Killed' or s.startswith(('STOP_REASON=','OPERATOR_SENT_SIGKILL=','NEXT_BOOT_FRAME_CAPTURE_READY=')) or phase or anchor or s.startswith(('340 (','GAME_PID=340','footprint_mb ','allowance_mb ','headroom_mb ','kills ','X86_FAULT','GAME_EXIT=')) or 'map toplevel' in s or 'no ack for frame' in s
  if keep:selected.append(f'{i+1}: {s}')
  if s.startswith('340 ('):
   # stat field3 starts after closing command parenthesis; fields14/15 are100Hz groupCPU.
   fields=s[s.rfind(')')+2:].split();user=int(fields[11]);system=int(fields[12]);threads=int(fields[17]);
   timestamp=datetime.datetime.strptime(lines[0],'%a %b %d %H:%M:%S UTC %Y').replace(tzinfo=datetime.timezone.utc)
   timeline.append({'source':name,'capture_utc':timestamp.isoformat(),'pid':340,'user_ticks':user,'system_ticks':system,'cpu_seconds':(user+system)/100,'threads':threads})
 dst=OUT/'game-progress'/name;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_text('\n'.join(selected)+'\n')
 records.append({'source':str(src.relative_to(WORK.parent)),'destination':str(dst.relative_to(OUT)),'source_sha256':hashlib.sha256(data).hexdigest(),'source_bytes':len(data),'archived_sha256':h(dst),'mode':'numbered phase lines, targetPID340 stat, aggregate memory and selected executable-map anchors only'})
timeline.sort(key=lambda x:x['capture_utc'])
for previous,current in zip(timeline,timeline[1:]):
 wall=(datetime.datetime.fromisoformat(current['capture_utc'])-datetime.datetime.fromisoformat(previous['capture_utc'])).total_seconds()
 current['seconds_since_previous_capture']=wall;current['cpu_seconds_since_previous_capture']=round(current['cpu_seconds']-previous['cpu_seconds'],3)
 current['average_host_cpu_cores_since_previous_capture']=round(current['cpu_seconds_since_previous_capture']/wall,4) if wall else None
(OUT/'cpu-timeline.json').write_text(json.dumps({'scope':'PID340 group CPU accounting; not per-thread sums, useful-progress proof, or frame-rate result','samples':timeline},indent=2)+'\n')
# Preserve a narrow sampled-CPU summary, with source hashes instead of raw trace/XML.
profile_dir=PERF/'build4447'
profile=json.loads((profile_dir/'startup-profile-summary.json').read_text())
summary={k:profile[k] for k in ['samples','missing_backtrace_rows','sample_weight_seconds','sample_span_seconds','top_self','top_inclusive','top_stacks']}
summary['threads']=[{**t,'thread':re.sub(r' \(0x[0-9a-f]+\)', '',t['thread'])} for t in profile['threads'][:2]]
summary['capture']={'tool':'Xcode Instruments Time Profiler','instruments_version':'27.0 (27A266a)','device_model':'iPad Air 13-inch (M2)','process':'Linux for iPad','native_pid':21341,'game_pid':340,'start_utc':'2026-10-16T12:07:55.066Z','end_utc':'2026-10-16T12:08:00.864Z','configured_time_limit_seconds':5,'trace_duration_seconds':5.797403,'end_reason':'Time limit reached'}
summary['qualification']='Sampled native CPU during this bounded startup interval, not elapsed-time share, GPU workload, FPS or playback evidence. Trace ending does not mean game exit.'
summary['source_files']=[{'path':str((profile_dir/n).relative_to(WORK.parent)),'sha256':h(profile_dir/n),'bytes':(profile_dir/n).stat().st_size} for n in ['startup-profile-summary.json','startup-profile-samples.xml','startup-profile-toc.xml','startup-profile.log']]
(OUT/'profiler').mkdir(exist_ok=True)
(OUT/'profiler/startup-profile-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
records.append({'destination':'profiler/startup-profile-summary.json','archived_sha256':h(OUT/'profiler/startup-profile-summary.json'),'mode':'selected summary fields and two relevant threads; capture metadata without user/device identifiers; source hashes embedded'})
(OUT/'source-evidence.json').write_text(json.dumps({'files':records},indent=2)+'\n')
(OUT/'provenance.json').write_text(json.dumps({'status':'frozen: manually stopped build4447 startup, no visible game frame','build':4447,'source_fix_commit':'9beb5dea2eeaffd7ebd27952592cba658fe12bb4','frozen_release_receipt_commit':'a2bd0e2bf6e14b10cc4cb70289761bd0467db302','game_pid':340,'game_launch_utc':'2026-10-16T11:57:56Z','game_stop_utc':'2026-10-16T12:13:22Z','run_elapsed_seconds':926,'stop_cause':'operator SIGKILL after verified target PID; GAME_EXIT=137 is manual, not a spontaneous crash','last_included_unity_phase_utc':'2026-10-16T12:12:01.499Z','last_included_unity_phase':'FMOD failed to initialize output device, error33','frozen':True,'visible_game_frame_qualified':False,'gameplay_qualified':False,'performance_qualified':False,'graphics_parser_tests':'synthetic only','frozen_build_and_mono_archives_modified':False,'proprietary_binary_bytes_included':False},indent=2)+'\n')
(OUT/'SHA256SUMS').write_text(''.join(h(f)+'  '+str(f.relative_to(OUT))+'\n' for f in sorted(OUT.rglob('*')) if f.is_file() and f.name!='SHA256SUMS' and '__pycache__' not in f.parts))
shutil.copytree(OUT,REPO,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__'))
for root in [OUT,REPO]:
 for line in (root/'SHA256SUMS').read_text().splitlines():
  digest,name=line.split('  ',1);assert h(root/name)==digest,name
 for f in root.rglob('*.json'):json.loads(f.read_text())
print(json.dumps({'files':len((OUT/'SHA256SUMS').read_text().splitlines())+1,'bytes':sum(f.stat().st_size for f in OUT.rglob('*') if f.is_file()),'sha256sums':h(OUT/'SHA256SUMS'),'frozen':True}))
