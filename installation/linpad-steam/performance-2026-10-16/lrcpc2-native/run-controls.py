from pathlib import Path
import subprocess,os,signal,time,json,hashlib
base=Path(__file__).resolve().parent
results=[]
cases=[('tier1-inline',{'ISH_JIT':'1','ISH_JIT_TIER':'1','ISH_JIT_COMPACT':'0'}),('gadget',{'ISH_JIT':'0'}),('tier1-compact',{'ISH_JIT':'1','ISH_JIT_TIER':'1','ISH_JIT_COMPACT':'1'}),('tier2',{'ISH_JIT':'1','ISH_JIT_TIER':'2','ISH_JIT_COMPACT':'0'})]
for name,setting in cases:
 out=base/(name+'.txt');cmd=['work/linpad-cli/ish','-r','work/performance/vulkan-repro-root','/opt/jit-lrcpc2-probe'];start=time.monotonic()
 with out.open('wb') as f:
  p=subprocess.Popen(cmd,stdout=f,stderr=subprocess.STDOUT,env={**os.environ,**setting},start_new_session=True)
  timeout=False
  try:p.wait(timeout=45)
  except subprocess.TimeoutExpired:timeout=True;os.killpg(p.pid,signal.SIGKILL);p.wait()
 text=out.read_text(errors='replace');excerpt='\n'.join(x for x in text.splitlines() if x.startswith('LRCPC2_') or x.startswith('FAIL'))
 (base/(name+'-excerpt.txt')).write_text(excerpt+'\n')
 result={'case':name,'environment':setting,'exit':p.returncode,'timeout':timeout,'elapsed':time.monotonic()-start,'excerpt':excerpt,'raw_sha256':hashlib.sha256(out.read_bytes()).hexdigest()};results.append(result);print(json.dumps(result),flush=True)
 (base/'guest-results.json').write_text(json.dumps(results,indent=2)+'\n')
