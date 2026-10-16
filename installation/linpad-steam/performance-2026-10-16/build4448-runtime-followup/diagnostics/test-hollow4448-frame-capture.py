#!/usr/bin/env python3
"""Host-only fake-file checks for the prepared capture script; no device use."""
from pathlib import Path
import json
import os
import subprocess
import tempfile
import threading
import time

base = Path(__file__).resolve().parent
script = base / 'serve/hollow4448-frame-capture'
checks = []
with tempfile.TemporaryDirectory(prefix='linpad-frame-fixture-') as directory:
    root = Path(directory)
    log = root / 'frames.txt'; compositor = root / 'ishwl.log'; unity = root / 'unity.log'
    runtime = root / 'views'; runtime.mkdir()
    log.write_text('1000 9 1 640 360\n')
    compositor.write_text("[100] map toplevel 9 'Hollow Knight' hollow_knight\n")
    unity.write_text('Vulkan renderer selected\n')
    (runtime / 'v9-1.buf').touch()
    capture = root / 'sample'
    env = dict(os.environ, LINPAD_FRAME_LOG=str(log), LINPAD_COMPOSITOR_LOG=str(compositor),
               LINPAD_UNITY_LOG=str(unity), LINPAD_VIEW_RUNTIME=str(runtime), LINPAD_FRAME_CAPTURE=str(capture))
    def run(*args):
        return subprocess.run(['sh', str(script), *args], env=env, text=True, capture_output=True)
    result = run('status'); assert result.returncode == 0 and 'VIEW_ID=9' in result.stdout
    checks.append('status identifies titled view with current buffer')
    def append():
        time.sleep(.35)
        with log.open('a') as file:
            file.write('1350 3 99 20 20\n1350 9 2 640 360\n1366 9 3 640 360\n')
    writer = threading.Thread(target=append); writer.start()
    result = run('capture', 'auto', '1'); writer.join()
    assert result.returncode == 0, result.stderr
    assert 'VIEW_BUFFER_CHANGED' not in result.stdout
    meta = dict(x.split('=', 1) for x in capture.with_suffix('.meta').read_text().split())
    assert meta['view_id'] == '9' and meta['start_line'] == '1' and meta['end_line'] == '4'
    rows = capture.with_suffix('.txt').read_text().splitlines(); assert len(rows) == 3
    checks.append('bounded capture excludes old rows and retains metadata for exact view filtering')
    result = run('capture', '9', '1'); assert result.returncode == 73
    checks.append('existing capture cannot be overwritten')
    env['LINPAD_FRAME_CAPTURE'] = str(root / 'second')
    result = run('capture', '9', '61'); assert result.returncode == 64
    checks.append('capture duration bounded at sixty seconds')
    result = run('capture', '8', '1'); assert result.returncode == 65
    checks.append('unproven view rejected')
    with compositor.open('a') as file:
        file.write("[101] map toplevel 10 'Hollow Knight' hollow_knight\n")
    (runtime / 'v10-1.buf').touch()
    result = run('capture', 'auto', '1'); assert result.returncode == 65
    checks.append('ambiguous active game views rejected')
    (runtime / 'v9-1.buf').unlink(); (runtime / 'v10-1.buf').unlink()
    result = run('capture', 'auto', '1'); assert result.returncode == 65
    checks.append('historical maps without a current buffer rejected')
print(json.dumps({'scope': 'Synthetic host-file checks only; no physical cadence result', 'passed': checks}, indent=2))
