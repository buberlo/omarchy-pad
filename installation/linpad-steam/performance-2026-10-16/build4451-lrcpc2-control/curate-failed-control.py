#!/usr/bin/env python3
"""Preserve the failed physical feature-mask control; no device/Git mutations."""
from pathlib import Path
import datetime,hashlib,json,shutil
BASE=Path(__file__).resolve().parent
PERF=BASE.parent
OUT=BASE/'curated'
REPO=PERF.parent/'LinPad/installation/linpad-steam/performance-2026-10-16/build4451-lrcpc2-control'
OUT.mkdir(exist_ok=True)
sha=lambda data:hashlib.sha256(data).hexdigest()
records=[]
def copy(src,name=None):
 src=Path(src);name=name or src.name;data=src.read_bytes();p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
 records.append({'source':str(src.relative_to(PERF.parent.parent)),'destination':name,'bytes':len(data),'sha256':sha(data),'mode':'byte-identical'})
 return p
for name in ['baseline-launch.sh','candidate-launch.sh','launch-control.patch','baseline-inputs.json','hollow4451-frame-capture','hollow4451-menu-cadence','parse-hollow-frame-window.py']:
 copy(BASE/name)
copy(Path(__file__))
original=json.loads((BASE/'launch-provenance.json').read_text())
original['candidate_execution']={'status':'failed_before_Unity_log','utc':'2026-10-16T13:57:22Z','guest_pid':1703,'guest_signal':11,'shell_exit':139,'candidate_cadence':None}
(OUT/'launch-provenance.json').write_text(json.dumps(original,indent=2)+'\n')
fail=PERF/'reports/hollow4451-rcpc2-exit-1.txt';text=fail.read_text()
assert 'GAME_EXIT=139' in text and "can't open '/tmp/hollow4451-rcpc2-unity.txt'" in text
copy(fail,'candidate-exit.txt')
console=PERF/'build4451/device-console.log';raw=console.read_bytes();lines=raw.decode(errors='replace').splitlines()
chosen=[]
for i,line in enumerate(lines):
 if line.startswith('ish crash: pid 1703 tgid 1703 '):
  chosen.append((i+1,line))
  if i+1<len(lines) and lines[i+1].startswith('ish crash:   last syscalls'):
   chosen.append((i+2,lines[i+1]))
assert len(chosen)==2
clean=lambda s:''.join(c if ord(c)>=32 or c=='\t' else f'\\x{ord(c):02x}' for c in s)
(OUT/'candidate-guest-fault-excerpt.txt').write_text('\n'.join(f'{n}: {clean(t)}' for n,t in chosen)+'\n')
records.append({'source':str(console.relative_to(PERF.parent.parent)),'destination':'candidate-guest-fault-excerpt.txt','snapshot_bytes':len(raw),'snapshot_sha256':sha(raw),'source_lines':[n for n,_ in chosen],'mode':'two numbered guest-fault lines; control character rendered as literal escape; no process/maps dump'})
copy(PERF/'build4451/curated/baseline-stop-excerpt.txt','baseline-stop-excerpt.txt')
meta={'curation_frozen':True,'captured_at_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'build':'4451','app_source_commit':'45f3e230cac85e946913279094f5e5afbb11c21d','feature_mask':'disablelrcpc2','outcome':'guest_exit139_before_Unity_log','execution_utc':'2026-10-16T13:57:22Z','guest_pid':1703,'fault':{'signal':11,'code':0,'address':'0x80013505aa34','pc':'0x80013505aa34','lr':'0x800124041214','scope':'Anonymous FEX-generated code location; cause not established by these lines.'},'candidate_cadence':None,'recommendation':'Do not adopt this failed candidate launch as a working configuration.','baseline':{'archive':'../build4451','interval_utc':['2026-10-16T13:55:24Z','2026-10-16T13:55:54Z'],'rows':212,'seconds':30,'composited_deliveries_per_second':212/30,'scene':'foreground German title menu','resolution':[659,540],'operator_stop_utc':'2026-10-16T13:56:50Z/2026-10-16T13:56:53Z','signals':['SIGTERM','SIGKILL'],'guest_pid':348},'records':records,'scope':'Frozen failed-experiment evidence. App host was not reported to crash; the guest exited139. No candidate Unity window, cadence, gameplay, audio or performance improvement is claimed. Subsequent diagnosis belongs in a separate follow-up.'}
(OUT/'provenance.json').write_text(json.dumps(meta,indent=2)+'\n')
(OUT/'README.md').write_text('''# Failed build 4451 FEX feature-mask control

At 13:57:22 UTC on 2026-10-16, the same signed app was used to launch Hollow Knight with `FEX_HOSTFEATURES=disablelrcpc2`. The guest immediately exited 139, before the candidate Unity log existed. The host console identifies guest PID 1703, signal 11, code 0, anonymous generated-code PC/address `0x80013505aa34` and LR `0x800124041214`. These lines do not establish the underlying defect. This is a failed diagnostic configuration, not a recommended setting.

The candidate launcher is byte-for-byte identical to the preserved baseline after removing its one added feature-mask line and restoring its unique output/log prefix. Exact hashes and the diff are included. No native translator or app source change was made for this experiment.

The [baseline build 4451 checkpoint](../build4451/VERIFICATION.md) independently reached the foreground German title menu at actual 659×540. Its 13:55:24–54 capture recorded 212 rows over 30 seconds (7.0667 composited deliveries/s), with no sequence gaps or selected-view remap. The baseline was then manually stopped with TERM/KILL at 13:56:50–53 for this test. Screenshots, frame rows, parser and original menu report live in that baseline archive rather than being duplicated here.

There is no candidate cadence measurement, so no before/after performance ratio is valid. The feature mask was intended to avoid the measured LRCPC2 fallback family while preserving ordered memory operations; this failed execution does not prove that the intended alternate instruction path is correct. Later local/native diagnosis must be recorded separately.

This folder is frozen historical evidence. Its checksum list covers only the lightweight files here. Raw device console/process/maps data and proprietary game binaries remain outside Git. The proc counter observations and original signing/SMC/source qualification are in the baseline checkpoint. No private-release upload or normal gameplay acceptance is claimed by this folder.
''')
(OUT/'SHA256SUMS').write_text(''.join(sha(p.read_bytes())+'  '+str(p.relative_to(OUT))+'\n' for p in sorted(OUT.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
shutil.copytree(OUT,REPO,dirs_exist_ok=True)
for root in [OUT,REPO]:
 for line in (root/'SHA256SUMS').read_text().splitlines():
  h,n=line.split('  ',1);assert sha((root/n).read_bytes())==h,n
 for p in root.rglob('*.json'):json.loads(p.read_text())
print(json.dumps({'files':sum(p.is_file() for p in OUT.rglob('*')),'checksum_list_sha256':sha((OUT/'SHA256SUMS').read_bytes()),'repo':str(REPO)},indent=2))
