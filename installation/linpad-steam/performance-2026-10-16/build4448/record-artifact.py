#!/usr/bin/env python3
"""Record an already built/signed/packaged 4448; no device or network actions."""
from pathlib import Path
import hashlib, json, plistlib, subprocess, zipfile

base = Path(__file__).resolve().parent
repo = base.parents[1] / 'LinPad'
commit = (base / 'source-commit.txt').read_text().strip()
app = base / 'Payload/iSH ARM64.app'
info = plistlib.loads((app / 'Info.plist').read_bytes())
assert info['CFBundleVersion'] == '4448'
assert info['CFBundleIdentifier'] == 'com.konradkern.linpad'
assert info['CFBundleShortVersionString'] == '1.4.0'

def digest_stream(stream):
    h = hashlib.sha256()
    for chunk in iter(lambda: stream.read(1024 * 1024), b''):
        h.update(chunk)
    return h.hexdigest()

def sha(path):
    with path.open('rb') as stream:
        return digest_stream(stream)

def run(*args):
    return subprocess.check_output(args, cwd=repo)

subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
ent = plistlib.loads(subprocess.check_output(['codesign', '-d', '--entitlements', ':-', str(app)]))
keys = ['com.apple.developer.kernel.increased-memory-limit', 'com.apple.developer.kernel.extended-virtual-addressing']
assert all(ent.get(k) is True for k in keys)
(base / 'signed-entitlements.plist').write_bytes(plistlib.dumps(ent))
exe = app / info['CFBundleExecutable']
dwarf = base / 'iSH ARM64.app.dSYM/Contents/Resources/DWARF' / info['CFBundleExecutable']
uuid = subprocess.check_output(['xcrun', 'dwarfdump', '--uuid', str(exe)], text=True).split()[1]
assert uuid == subprocess.check_output(['xcrun', 'dwarfdump', '--uuid', str(dwarf)], text=True).split()[1]
sources = [x for x in run('git', 'diff-tree', '--no-commit-id', '--name-only', '-r', commit).decode().splitlines() if x]
source_records = []
for name in sources:
    blob = run('git', 'show', commit + ':' + name)
    assert (repo / name).read_bytes() == blob, name
    out = base / 'source-snapshot' / name
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    source_records.append({'path': name, 'sha256': sha(out)})
(base / 'source-changes.patch').write_bytes(run('git', 'diff', commit + '^', commit, '--', *sources))
assert '** BUILD SUCCEEDED **' in (base / 'build.log').read_text()
archives = []
for name, tree in [('LinPad-1.4.0-build4448-Konrad.ipa', 'Payload'), ('LinPad-1.4.0-build4448-dSYM.zip', 'iSH ARM64.app.dSYM')]:
    original = {str(p.relative_to(base)): p for p in (base / tree).rglob('*') if p.is_file()}
    with zipfile.ZipFile(base / name) as archive:
        assert archive.testzip() is None
        assert len(archive.namelist()) == len(set(archive.namelist()))
        assert {x.filename for x in archive.infolist() if not x.is_dir()} == set(original)
        for member, path in original.items():
            with archive.open(member) as stream:
                assert digest_stream(stream) == sha(path), member
    archives.append({'name': name, 'bytes': (base / name).stat().st_size, 'sha256': sha(base / name), 'crc_and_bundle_contents_verified': True})
manifest = {'build': '4448', 'version': info['CFBundleShortVersionString'], 'bundle_id': info['CFBundleIdentifier'], 'source_commit': commit, 'source_files': source_records, 'source_patch_sha256': sha(base / 'source-changes.patch'), 'build_verified': True, 'build_log_sha256': sha(base / 'build.log'), 'signature_verified': True, 'memory_entitlements': {k: ent[k] for k in keys}, 'app_and_dsym_uuid': uuid, 'executable_sha256': sha(exe), 'dwarf_sha256': sha(dwarf), 'rootfs_sha256': sha(app / 'root.tar.gz'), 'rootfs_unchanged_from4447': sha(app / 'root.tar.gz') == sha(base.parent / 'build4447/Payload/iSH ARM64.app/root.tar.gz'), 'archives': archives, 'device_validation': 'Pending; update only from physical build4448 evidence.', 'guest_runtime_note': 'Installed FEX, Mesa, EGL, Steam and game data are separate from the IPA.'}
assert not (base / 'build-manifest.json').exists(), 'Preserve existing manifest'
(base / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps({'uuid': uuid, 'source': commit, 'archives': archives}, indent=2))
