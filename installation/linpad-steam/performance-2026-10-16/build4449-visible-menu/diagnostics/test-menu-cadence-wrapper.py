#!/usr/bin/env python3
"""Replay wrapper flow with synthetic files and stub waits/network; no timing claim."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile

base = Path(__file__).resolve().parent
checks, created = [], []
with tempfile.TemporaryDirectory(prefix='linpad-cadence-wrapper-') as directory:
    p = Path(directory)
    bindir = p/'bin'; bindir.mkdir()
    views = p/'views'; views.mkdir()
    log = p/'frames'; log.write_text('100 9 1 20 20\n')
    compositor = p/'compositor'; compositor.write_text("[1] map toplevel 9 'Hollow Knight' app\n")
    unity = p/'unity'; unity.write_text('Desktop is 960 x 540\n')
    (views/'v9-1.buf').touch()
    (bindir/'sleep').write_text('''#!/bin/sh
printf '%s\n' "$1" >> "$FAKE_SLEEPS"
if test "$1" = 30; then printf '200 9 2 20 20\n' >> "$LINPAD_FRAME_LOG"; fi
''')
    (bindir/'wget').write_text('''#!/bin/sh
printf '%s\n' "$*" >> "$FAKE_POSTS"
exit 0
''')
    for f in bindir.iterdir(): f.chmod(0o755)
    env = dict(os.environ, PATH=str(bindir)+':'+os.environ['PATH'],
        LINPAD_FRAME_HELPER=str(base/'serve/hollow4449-frame-capture'),
        LINPAD_FRAME_LOG=str(log), LINPAD_COMPOSITOR_LOG=str(compositor),
        LINPAD_UNITY_LOG=str(unity), LINPAD_VIEW_RUNTIME=str(views),
        FAKE_SLEEPS=str(p/'sleeps'), FAKE_POSTS=str(p/'posts'))
    def run(view):
        result = subprocess.run(['sh', str(base/'serve/hollow4449-menu-cadence'), view],
            env=env, text=True, capture_output=True, timeout=10)
        for line in result.stdout.splitlines():
            if line.startswith('REPORT_FILE='):
                created.append(Path(line.split('=',1)[1]).parent)
        return result
    try:
        r = run('auto'); assert r.returncode == 0, r.stderr
        assert (p/'sleeps').read_text().splitlines() == ['5','30']
        checks.append('exact five-second pre-delay and thirty-second collector request, using stub clock waits')
        assert 'BEGIN_CAPTURE_meta' in r.stdout and 'view_id=9 requested_seconds=30' in r.stdout
        assert 'BEGIN_CAPTURE_txt\n200 9 2 20 20' in r.stdout
        checks.append('exact title/live-buffer identification and delimited metadata/status/new rows preserved')
        assert 'CAPTURE_EXIT=0' in r.stdout and 'REPORT_POST_EXIT=0' in r.stdout
        assert len((p/'posts').read_text().splitlines()) == 1
        checks.append('completed report sent once through bounded POST path')
        r = run('9'); assert r.returncode == 0 and created[0] != created[1]
        checks.append('repeat capture uses a new directory and preserves earlier files')
        r = run('8'); assert r.returncode == 65 and 'CAPTURE_EXIT=65' in r.stdout and 'FILE_NOT_CREATED=1' in r.stdout
        checks.append('unproven view fails and failure report is still collected')
    finally:
        for path in created: shutil.rmtree(path)
print(json.dumps({'scope':'Host synthetic regular files and stub waits/network only. No device, real timing or FPS result.',
                  'checks':checks,'passed':len(checks)}, indent=2))
