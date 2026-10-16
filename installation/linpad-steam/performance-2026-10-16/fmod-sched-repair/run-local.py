#!/usr/bin/env python3
"""Run already-staged diagnostic probes against two caller-supplied local CLIs.
Usage: run-local.py OLD_ISH NEW_ISH ROOTFS OUTDIR
Stage the built probes and launch binary in ROOTFS/opt/fmod-probe first.
The exact Unity input must already exist at ROOTFS/opt/UnityPlayer.so. Never copy
that proprietary input into the evidence archive. This script does not stage it.
"""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys,time
if len(sys.argv)!=5: raise SystemExit(__doc__)
old,new,root,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True);rows=[]
for state,cli in [('before',old),('after',new)]:
 for kind,args in [('native',['/opt/fmod-probe/sched-priority-arm64']),('fex',['/opt/fmod-probe/launch','/opt/fmod-probe/sched-priority-x86']),('unity-helper',['/opt/fmod-probe/launch','/opt/fmod-probe/unity-fmod-thread-probe'])]:
  start=time.monotonic();raw=out/f'{state}-{kind}.txt'
  with raw.open('w') as f:
   try:
    p=subprocess.run([str(cli.resolve()),'-r',str(root.resolve()),*args],env={**os.environ,'ISH_JIT':'1'},stdout=f,stderr=subprocess.STDOUT,timeout=45);rc=p.returncode;timed=False
   except subprocess.TimeoutExpired:rc=None;timed=True
  lines=raw.read_text(errors='replace').splitlines();excerpt='\n'.join(re.sub(r'(?<=e33)\d+ call .*','',s) for s in lines if s.startswith(('SCHED_','FMOD_ATTR','UNITY_')))+'\n'
  (out/f'{state}-{kind}-excerpt.txt').write_text(excerpt)
  row={'state':state,'kind':kind,'exit':rc,'timeout':timed,'elapsed':round(time.monotonic()-start,3),'cli_sha256':hashlib.sha256(cli.read_bytes()).hexdigest(),'raw_sha256':hashlib.sha256(raw.read_bytes()).hexdigest()};rows.append(row);print(json.dumps(row))
(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
