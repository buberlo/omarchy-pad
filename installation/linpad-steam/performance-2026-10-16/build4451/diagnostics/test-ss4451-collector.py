#!/usr/bin/env python3
"""Run the real collector with only /proc/temp paths and OS I/O stubbed."""
from pathlib import Path
import hashlib,json,os,subprocess,tempfile
BASE=Path(__file__).resolve().parent
source=(BASE/'serve/ss4451-probe').read_text()
keys='hit miss_cold miss_collision miss_generation miss_generation_same_pc shared_hit persistent_fill oneshot_generation oneshot_pressure untracked_oom path_negative path_exit path_nomem'.split()
def context(pid=271,mmu=9,count=100):
 return 'context tid=%s tgid=%s mmu=%s reset=2 sampled=1 busy_samples=0 '%(pid,pid,mmu)+' '.join('%s=%s'%(k,count if k in ['hit','path_exit'] else 0) for k in keys)+'\n'
header='supported 1\nenabled 1\nversion 1\ntruncated 0\ncontext_allocation_failed 0\n'
sample='sample tid=271 mmu=9 serial=1 pc=0x8001000 insn=d51b4400 readable=1 path=1 miss=0\n'
good1=header+context()+sample+context(2719)+sample.replace('tid=271 ','tid=2719 ')
good2=header+context(count=200)+sample+context(2719)+sample.replace('tid=271 ','tid=2719 ')
cases=[
 ('valid_tgid_tick_tolerance',good1,good2,100,0,True),
 ('disabled',good1.replace('enabled 1','enabled 0'),good2,100,70,False),
 ('truncated',good1.replace('truncated 0','truncated 1'),good2,100,70,False),
 ('wrong_sample_identity',good1.replace('sample tid=271 ','sample tid=999 '),good2,100,70,False),
 ('changed_mmu',good1,good2.replace('mmu=9','mmu=10'),100,70,False),
 ('counter_decrease',good2,good1,100,70,False),
 ('capped_maps',good1,good2,210000,0,True),
 ('oversized_report',good1+'unexpected '+('X'*520000)+'\n',good2+'unexpected '+('X'*520000)+'\n',100,75,False),
]
results=[]
with tempfile.TemporaryDirectory(prefix='ss4451-tests-') as temp:
 for name,s1,s2,maps_size,want_exit,eligible in cases:
  d=Path(temp)/name;proc=d/'proc';bindir=d/'bin';bindir.mkdir(parents=True)
  (proc/'ish').mkdir(parents=True);(proc/'271').mkdir()
  (proc/'ish/jit-ss').write_text(s1);(d/'snapshot1').write_text(s1);(d/'snapshot2').write_text(s2)
  (proc/'271/cmdline').write_bytes(b'/opt/fex/FEX\0/opt/Steam/hollow_knight.x86_64\0-force-vulkan\0')
  for i,tick in enumerate([100,101,100],1):
   fields=['0']*40;fields[0]='R';fields[19]=str(tick)
   (d/('stat'+str(i))).write_text('271 (FEX test) '+' '.join(fields)+'\n')
  (proc/'271/stat').write_text((d/'stat1').read_text())
  (proc/'271/maps').write_text('0'*maps_size)
  (bindir/'head').write_text('''#!/usr/bin/env python3
import os,sys,subprocess,pathlib
d=pathlib.Path(os.environ['CASE_DIR']);args=sys.argv[1:];p=args[-1]
for key,suffix in [('snapshot','/ish/jit-ss'),('stat','/271/stat')]:
 if p.endswith(suffix):
  counter=d/(key+'-count');n=int(counter.read_text())+1 if counter.exists() else 1;counter.write_text(str(n));args[-1]=str(d/(key+str(n)));break
raise SystemExit(subprocess.run(['/usr/bin/head']+args).returncode)
''')
  (bindir/'sleep').write_text('#!/bin/sh\nexit 0\n')
  (bindir/'wget').write_text('''#!/usr/bin/env python3
import sys,os,pathlib,shutil
d=pathlib.Path(os.environ['CASE_DIR']);p=next(a.split('=',1)[1] for a in sys.argv if a.startswith('--post-file='))
shutil.copyfile(p,d/'posted.txt')
with (d/'post-count').open('a') as f:f.write('1\\n')
''')
  for p in bindir.iterdir():p.chmod(0o755)
  script=d/'collector';script.write_text(source.replace('/proc/',str(proc)+'/').replace('/tmp/ss4451-',str(d/'ss4451-')))
  env=dict(os.environ,PATH=str(bindir)+':'+os.environ['PATH'],CASE_DIR=str(d))
  r=subprocess.run(['sh',str(script),'271'],env=env,capture_output=True,text=True,timeout=15)
  assert r.returncode==want_exit,(name,r.returncode,r.stdout,r.stderr)
  posted=d/'posted.txt'
  if want_exit==75:
   assert not posted.exists() and 'REPORT_TOO_LARGE' in r.stderr
  else:
   report=posted.read_text()
   assert (d/'post-count').read_text()=='1\n'
   assert ('MAIN_DELTA_ELIGIBLE=1' in report)==eligible,name
   assert 'context tid=2719 ' not in report and 'sample tid=2719 ' not in report
   if eligible:assert 'hit=100 ' in report.split('MAIN_DELTA tid=',1)[1]
   if name=='capped_maps':assert 'MAPS_TRUNCATED=1' in report and len(report)<210000
  results.append({'case':name,'exit':r.returncode,'passed':True})
out=BASE/'jit-ss-diagnostics-review';out.mkdir(exist_ok=True)
(out/'collector-tests.json').write_text(json.dumps({'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'scope':'Synthetic process/counter files; actual collector shell and awk logic. Reads, waits and network stubbed. No device or performance claim.','results':results},indent=2)+'\n')
print(json.dumps({'passed':len(results),'source_sha256':hashlib.sha256(source.encode()).hexdigest()},indent=2))
