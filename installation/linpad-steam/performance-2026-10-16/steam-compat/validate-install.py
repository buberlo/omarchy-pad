#!/usr/bin/env python3
"""Check the installer and read-only verifier against a temporary fake library."""
from pathlib import Path
import os
import subprocess
import tempfile

base = Path(__file__).resolve().parent
for name in ('install.sh', 'verify-native.sh'):
    subprocess.run(['sh', '-n', str(base / name)], check=True)
checks = ['PASS installer and verifier shell syntax']

with tempfile.TemporaryDirectory(prefix='linpad-install-check-') as tmp:
    library = Path(tmp).resolve() / 'Steam library with spaces'
    apps = library / 'steamapps'
    apps.mkdir(parents=True)
    marker = library / 'unrelated-marker.txt'
    marker.write_text('untouched')
    env = os.environ.copy()
    env['LINPAD_STEAM_ROOT'] = str(library)

    def run(script, *args, expected=0):
        result = subprocess.run(['sh', str(base / script), *args], env=env,
                                text=True, capture_output=True)
        assert result.returncode == expected, (result.returncode, result.stdout, result.stderr)
        return result.stdout

    run('install.sh')
    target = library / 'compatibilitytools.d/linpad-fex-native'
    names = ('compatibilitytool.vdf', 'toolmanifest.vdf', 'run', 'launch-game')
    assert set(p.name for p in target.iterdir()) == set(names)
    for name in names:
        assert (target / name).read_bytes() == (base / 'linpad-fex-native' / name).read_bytes()
    assert os.access(target / 'run', os.X_OK) and os.access(target / 'launch-game', os.X_OK)
    checks.append('PASS four exact tool files installed with executable launchers')
    assert 'Already installed:' in run('install.sh')
    assert not (library / 'linpad-tool-backups').exists()
    checks.append('PASS identical reinstall makes no backup or content changes')
    old = '#!/bin/sh\nprintf old-tool\n'
    (target / 'run').write_text(old)
    run('install.sh')
    backups = list((library / 'linpad-tool-backups').iterdir())
    assert len(backups) == 1 and (backups[0] / 'run').read_text() == old
    assert marker.read_text() == 'untouched'
    checks.append('PASS replacement preserves previous tool; unrelated marker unchanged')
    manifest = apps / 'appmanifest_367520.acf'
    text = '''"AppState"
{
    "appid" "367520"
    "installdir" "Hollow Knight"
    "InstalledDepots"
    {
        "367523"
        {
            "manifest" "fixture-only"
        }
    }
}
'''
    manifest.write_text(text)
    game = apps / 'common/Hollow Knight/hollow_knight.x86_64'
    game.parent.mkdir(parents=True)
    header = bytearray(20)
    header[:7] = b'\x7fELF\x02\x01\x01'
    header[16:20] = b'\x02\x00\x3e\x00'
    game.write_bytes(header)
    run('verify-native.sh', str(library))
    assert manifest.read_text() == text and game.read_bytes() == header
    checks.append('PASS verifier accepts Linux depot plus x86-64 ELF and changes neither file')
    manifest.write_text(text.replace('367523', '367521'))
    run('verify-native.sh', str(library), expected=1)
    manifest.write_text(text)
    game.write_bytes(b'MZ-windows-fixture')
    run('verify-native.sh', str(library), expected=1)
    checks.append('PASS verifier rejects wrong depot and non-ELF executable')

report = '\n'.join(checks) + '\n\nOnly temporary synthetic directories/files were used; no Steam account or device access.\n'
(base / 'INSTALL-VALIDATION.txt').write_text(report)
print(report)
