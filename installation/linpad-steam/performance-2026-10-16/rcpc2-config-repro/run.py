import hashlib,json,os,pathlib,signal,subprocess,time
p=pathlib.Path('work/performance/rcpc2-config-repro')
results=[]
for name,feature,target,jit in [('hello-default','default','/opt/fex/hello-x86_64','1'),('hello-disabled','disablelrcpc2','/opt/fex/hello-x86_64','1'),('stack-default','default','/opt/mono-init/stack-limits-probe','1'),('stack-disabled','disablelrcpc2','/opt/mono-init/stack-limits-probe','1')]:
    cmd=['work/linpad-cli/ish','-r','work/performance/vulkan-repro-root','/opt/rcpc2-config-launch',feature,target]
    start=time.monotonic()
    with (p/(name+'.txt')).open('wb') as output:
        proc=subprocess.Popen(cmd,stdout=output,stderr=subprocess.STDOUT,env={**os.environ,'ISH_JIT':jit,'ISH_CRASHLOG':'2'},start_new_session=True)
        timeout=False
        try: proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            timeout=True;os.killpg(proc.pid,signal.SIGKILL);proc.wait()
    result={'case':name,'command':cmd,'jit':jit,'exit':proc.returncode,'timeout':timeout,'elapsed':round(time.monotonic()-start,3)}
    results.append(result);print(json.dumps(result),flush=True)
    (p/'results.json').write_text(json.dumps(results,indent=2)+'\n')
