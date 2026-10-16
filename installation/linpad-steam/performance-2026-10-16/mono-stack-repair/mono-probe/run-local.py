#!/usr/bin/env python3
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

base = Path('work/performance/mono-init-probe')
stage = sys.argv[1] if len(sys.argv) > 1 else 'gc'
label = sys.argv[2] + '-' if len(sys.argv) > 2 else ''
cases = [('0', 'before'), ('1', 'before'), ('1', 'after'), ('3221', 'before')]
if len(sys.argv) > 3 and sys.argv[3] == 'main-controls':
    cases = [('0', 'before'), ('3221', 'before')]
results = []
for flags, order in cases:
    name = f'{label}{stage}-{flags}-{order}'
    command = ['work/linpad-cli/ish', '-r', 'work/performance/vulkan-repro-root',
               '/opt/mono-init/launch', stage, flags, order]
    start = time.monotonic()
    with (base / (name + '.txt')).open('wb') as output:
        process = subprocess.Popen(command, env={**os.environ, 'ISH_JIT':'1'},
                                   stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        timed_out = False
        try:
            process.wait(timeout=45)
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
    excerpt = '\n'.join(line for line in (base / (name + '.txt')).read_text(errors='replace').splitlines()
                        if line.startswith('MONO_PROBE') or 'mscorlib' in line or 'assert' in line.lower())
    (base / (name + '-excerpt.txt')).write_text(excerpt + '\n')
    result = {'case':name, 'command':command, 'exit_code':process.returncode,
              'host_timeout_seconds':45, 'host_terminated_after_timeout':timed_out,
              'elapsed_seconds':round(time.monotonic()-start,3), 'excerpt':excerpt}
    results.append(result)
    print(json.dumps(result), flush=True)
    (base / (label + stage + '-results.json')).write_text(json.dumps(results, indent=2)+'\n')
files = ['work/linpad-cli/ish', 'work/performance/game-fault-report/libmonobdwgc-2.0.so',
         'work/performance/vulkan-repro-root/opt/UnityPlayer.so',
         'work/performance/vulkan-repro-root/opt/fex/FEX-apple-m1-munmap',
         'work/performance/vulkan-repro-root/opt/mono-init/probe',
         'work/performance/vulkan-repro-root/opt/mono-init/launch',
         'work/performance/game-fault-report/mscorlib.dll']
(base / (label + stage + '-provenance.json')).write_text(json.dumps({
    'scope':'Local macOS-hosted LinPad CLI only; not a physical iPad game test',
    'sdl_video_driver':'dummy; local no-display control',
    'files':{p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in files}
},indent=2)+'\n')
