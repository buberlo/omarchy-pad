#!/usr/bin/env python3
"""Verify and record already-built/signed/packaged LinPad 4446 artifacts.

Run after the parent has finished writing Payload, dSYM and both ZIP archives.
This script never builds, signs, packages, installs, launches, uploads or edits
repository files. It performs read-only signature/UUID checks, then creates
local provenance records. It refuses to overwrite an existing build manifest.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import plistlib
import re
import shutil
import subprocess
import zipfile

COMMIT = '72e1eef8b3125b39e7c852abf2d16bf7a1f82029'
BUILD = '4446'
SOURCE_FILES = [
    'fs/netlink.c', 'fs/netlink.h', 'fs/fd.h', 'fs/sock.c',
    'tests/manual/steam-compat/netlink-uevent-probe.c',
    'tests/manual/steam-compat/README.md',
]
MEMORY_KEYS = [
    'com.apple.developer.kernel.increased-memory-limit',
    'com.apple.developer.kernel.extended-virtual-addressing',
]


def streamsha(stream):
    h = hashlib.sha256()
    for chunk in iter(lambda: stream.read(1024 * 1024), b''):
        h.update(chunk)
    return h.hexdigest()


def sha(path):
    with path.open('rb') as stream:
        return streamsha(stream)


def run(argv, cwd=None):
    return subprocess.run(argv, cwd=cwd, check=True, capture_output=True)


def uuid_records(path):
    output = run(['xcrun', 'dwarfdump', '--uuid', str(path)]).stdout.decode()
    found = re.findall(r'UUID: ([0-9A-Fa-f-]+) \(([^)]+)\)', output)
    if not found:
        raise RuntimeError('No UUID found: ' + str(path))
    return sorted(({'uuid': value.upper(), 'architecture': arch} for value, arch in found),
                  key=lambda entry: (entry['architecture'], entry['uuid']))


def read_entitlements(app):
    result = run(['codesign', '-d', '--entitlements', ':-', str(app)])
    raw = result.stdout
    if raw.startswith(b'bplist'):
        entitlements = plistlib.loads(raw)
    else:
        start = raw.find(b'<?xml')
        if start < 0:
            start = raw.find(b'<plist')
        end = raw.find(b'</plist>', start)
        if start < 0 or end < 0:
            raise RuntimeError('codesign did not return an entitlement plist')
        entitlements = plistlib.loads(raw[start:end + len(b'</plist>')])
    for key in MEMORY_KEYS:
        if entitlements.get(key) is not True:
            raise RuntimeError('Missing approved memory entitlement: ' + key)
    return entitlements, result.stderr


def archive_record(path, prefix, original_files):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise RuntimeError('Duplicate archive names: ' + str(path))
        for name in names:
            parts = pathlib.PurePosixPath(name).parts
            if name.startswith('/') or '..' in parts or not name.startswith(prefix):
                raise RuntimeError('Unexpected archive member: ' + name)
            if name.startswith('__MACOSX/') or '/._' in name:
                raise RuntimeError('Unexpected resource-fork member: ' + name)
        members = {entry.filename for entry in archive.infolist() if not entry.is_dir()}
        if members != set(original_files):
            raise RuntimeError('Archive file membership differs from copied bundle: ' + str(path))
        bad = archive.testzip()
        if bad:
            raise RuntimeError('ZIP CRC failed: ' + bad)
        for member, original in original_files.items():
            with archive.open(member) as stream:
                if streamsha(stream) != sha(original):
                    raise RuntimeError('Packaged file differs: ' + member)
        return {'name': path.name, 'size_bytes': path.stat().st_size,
                'sha256': sha(path), 'zip_crc_verified': True,
                'zip_entries': len(names),
                'original_files_compared_count': len(original_files)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-log', type=pathlib.Path,
                        help='Default: build.log beside this script')
    args = parser.parse_args()
    base = pathlib.Path(__file__).resolve().parent
    workspace = base.parents[2]
    repo = workspace / 'work/LinPad'
    manifest_path = base / 'build-manifest.json'
    if manifest_path.exists():
        raise RuntimeError('Existing manifest preserved; update device records separately')
    build_log = args.build_log.resolve() if args.build_log else base / 'build.log'
    buildtext = build_log.read_text()
    if '** BUILD SUCCEEDED **' not in buildtext:
        raise RuntimeError('Build completion not established by log')
    app = base / 'Payload/iSH ARM64.app'
    info = plistlib.loads((app / 'Info.plist').read_bytes())
    if (str(info['CFBundleVersion']) != BUILD or
            info['CFBundleIdentifier'] != 'com.konradkern.linpad' or
            info.get('CFBundleShortVersionString') != '1.4.0'):
        raise RuntimeError('Unexpected bundle metadata')
    binary = app / info['CFBundleExecutable']
    dsym = base / 'iSH ARM64.app.dSYM'
    dwarf = dsym / 'Contents/Resources/DWARF' / info['CFBundleExecutable']
    uuids = uuid_records(binary)
    if uuid_records(dwarf) != uuids:
        raise RuntimeError('App/dSYM UUID mismatch')
    signature = run(['codesign', '--verify', '--deep', '--strict', '--verbose=2', str(app)])
    entitlements, entitlement_log = read_entitlements(app)
    source_base = run(['git', 'rev-parse', COMMIT + '^'], repo).stdout.decode().strip()
    source_data = {}
    for name in SOURCE_FILES:
        blob = run(['git', 'show', COMMIT + ':' + name], repo).stdout
        if (repo / name).read_bytes() != blob:
            raise RuntimeError('Build source differs from committed snapshot: ' + name)
        source_data[name] = blob
    patch = run(['git', 'diff', source_base, COMMIT, '--', *SOURCE_FILES], repo).stdout
    # Verify the complete regular-file contents of each packaged bundle against
    # the copied product; codesign separately checks the signed app directory.
    app_files = {str(p.relative_to(base)): p for p in app.rglob('*') if p.is_file()}
    symbol_files = {str(p.relative_to(base)): p for p in dsym.rglob('*') if p.is_file()}
    archives = [
        archive_record(base / 'LinPad-1.4.0-build4446-Konrad.ipa', 'Payload/', app_files),
        archive_record(base / 'LinPad-1.4.0-build4446-dSYM.zip', 'iSH ARM64.app.dSYM/', symbol_files),
    ]
    evidence_root = workspace / 'work/performance/egl-display-audit'
    final_provenance = json.loads((evidence_root / 'local-netlink-final-provenance.json').read_text())
    for name in SOURCE_FILES[:-1]:
        key = 'work/LinPad/' + name
        if final_provenance['files'].get(key) != sha(repo / name):
            raise RuntimeError('Final regression source mismatch: ' + name)
    raw_final = workspace / 'work/performance/egl-display-audit/local-netlink-final-native.txt'
    if sha(raw_final) != final_provenance['files'][str(raw_final.relative_to(workspace))]:
        raise RuntimeError('Final regression raw log changed')
    native_text = (evidence_root / 'local-netlink-final-native-excerpt.txt').read_text()
    if 'NETLINK_UEVENT_PASS failures=0' not in native_text or final_provenance['exit'] != 0:
        raise RuntimeError('Final native regression did not pass')
    case_count = len(re.findall(r'^.* PASS errno=', native_text, re.MULTILINE))
    if case_count != 35:
        raise RuntimeError('Unexpected regression case count')
    evidence = {
        'netlink-uevent-before.txt': workspace / 'work/performance/reports/netlink-uevent-before.txt',
        'local-netlink-final-native-excerpt.txt': evidence_root / 'local-netlink-final-native-excerpt.txt',
        'local-netlink-final-provenance.json': evidence_root / 'local-netlink-final-provenance.json',
    }
    for stem in ['local-unity-sdl-udev', 'local-unity-sdl-1000', 'local-unity-sdl-200',
                 'local-netlink-after-udev', 'local-netlink-after-1000', 'local-netlink-after-200']:
        evidence[stem + '-excerpt.txt'] = evidence_root / (stem + '-excerpt.txt')
    for path in evidence.values():
        if not path.is_file():
            raise RuntimeError('Missing regression evidence: ' + str(path))
    prior_root = base.parent / 'build4445/Payload/iSH ARM64.app/root.tar.gz'
    same_root = sha(app / 'root.tar.gz') == sha(prior_root)
    now = datetime.datetime.now(datetime.timezone.utc).isoformat()
    verification = (base / 'VERIFICATION.md').read_text()
    marker = '<!-- ARTIFACT-RECORD-PENDING -->'
    if marker not in verification:
        raise RuntimeError('Initial verification marker missing; later notes preserved')
    # All required verification above is read-only. Only now write records.
    source_records = []
    for name, blob in source_data.items():
        dest = base / 'source-snapshot' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(blob)
        source_records.append({'path': name, 'sha256': sha(dest),
                               'matches_source_commit_and_postbuild_worktree': True})
    (base / 'source-changes.patch').write_bytes(patch)
    (base / 'signed-entitlements.plist').write_bytes(plistlib.dumps(entitlements))
    (base / 'entitlements-read.log').write_bytes(entitlement_log)
    (base / 'codesign-verification.txt').write_bytes(signature.stdout + signature.stderr)
    for name, path in evidence.items():
        shutil.copy2(path, base / name)
    (base / 'app-files.sha256').write_text(''.join(sha(p) + '  ' + name + '\n' for name, p in sorted(app_files.items())))
    (base / 'dsym-files.sha256').write_text(''.join(sha(p) + '  ' + name + '\n' for name, p in sorted(symbol_files.items())))
    manifest = {
        'build': BUILD, 'version': info['CFBundleShortVersionString'],
        'bundle_id': info['CFBundleIdentifier'], 'recorded_at_utc': now,
        'source_head': COMMIT, 'source_base': source_base, 'source_fix_commits': [COMMIT],
        'source_capture_note': 'The parent committed this fix before starting xcodebuild. These snapshots match committed blobs and the worktree rechecked after compilation; this recorder does not claim an independent pre-build dirty-state snapshot.',
        'source_files': source_records, 'source_patch_sha256': sha(base / 'source-changes.patch'),
        'build_verified': True, 'build_log_path': str(build_log), 'build_log_sha256': sha(build_log),
        'signing_verified': True, 'signing_evidence': 'codesign-verification.txt',
        'memory_entitlements': {key: entitlements[key] for key in MEMORY_KEYS},
        'entitlements_evidence': 'signed-entitlements.plist', 'uuid_records': uuids,
        'binary_uuid': uuids[0]['uuid'] if len(uuids) == 1 else None,
        'dsym_uuid': uuids[0]['uuid'] if len(uuids) == 1 else None,
        'executable_sha256': sha(binary), 'dsym_dwarf_sha256': sha(dwarf),
        'app_files_manifest': 'app-files.sha256', 'app_files_manifest_sha256': sha(base / 'app-files.sha256'),
        'dsym_files_manifest': 'dsym-files.sha256', 'dsym_files_manifest_sha256': sha(base / 'dsym-files.sha256'),
        'rootfs_sha256': sha(app / 'root.tar.gz'), 'rootfs_unchanged_from_build4445': same_root,
        'archives': archives, 'installed': None, 'launched': None, 'device_accepted': None,
        'physical_device_validation': 'Pending independent build4446 device validation; no prior-build device result is inherited.',
        'device_validation': {'boot': 'pending', 'native_netlink_probe': 'pending',
                              'exact_unity_sdl_init': 'pending', 'gpu_test': 'pending',
                              'rendered_game_frame_verified': False, 'gameplay_verified': False,
                              'steady_state_performance_qualified': False},
        'publication_status': 'Recorded locally; no upload or release by this script',
        'host_regressions': {
            'execution': 'Local macOS-hosted LinPad CLI, native ARM64 / x86 through FEX',
            'native_uevent': {'result': 'pass', 'case_count': case_count,
                             'log': 'local-netlink-final-native-excerpt.txt',
                             'provenance': 'local-netlink-final-provenance.json'},
            'negative_control': 'Prior CLI rejects protocol15 socket with errno97; netlink-uevent-before.txt',
            'embedded_unity_sdl': 'Haptic0x1000 and joystick0x200 failed before and return0 after the monitor fix. These after samples precede the final group-mask readback correction; the final native35-case run covers that committed source.',
            'libudev_boundary': 'Monitor construction succeeds; filter enable still returns-22 because BPF is unsupported. This is not full libudev event-delivery qualification.',
            'evidence': [{'name': name, 'sha256': sha(base / name)} for name in evidence],
        },
        'limitations': ['No host hotplug-event producer or guest-to-guest uevent delivery.',
                        'BPF filters and dynamic membership options are unsupported and return errors.',
                        'FEX, Mesa26, EGL25 and installed games remain separate guest data, not embedded in the IPA.',
                        '512 MiB JIT cache was verified on earlier installed builds; active4446 value needs device readback.'],
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    command = buildtext.split('\n\n', 1)[0]
    (base / 'build-summary.log').write_text('Local Release build4446\nSource: ' + COMMIT + '\n\n' + command + '\n\nBUILD SUCCEEDED; independent codesign verification passed.\nApp/dSYM UUID records: ' + json.dumps(uuids) + '\nFull build log SHA256: ' + sha(build_log) + '\nDevice acceptance pending.\n')
    result = ('Artifact verification completed at `' + now + '`. Build success, deep/strict developer signature, both memory entitlements, matching app/dSYM UUIDs, archive CRCs and packaged bundle bytes all pass. UUIDs: `' + ', '.join(x['uuid'] for x in uuids) + '`. Physical-device acceptance remains pending.\n\nThe embedded root archive ' + ('matches build4445.' if same_root else 'differs from build4445; review this before installation.') + ' See `build-manifest.json` and `SHA256SUMS` for exact provenance and artifact hashes.')
    verification = verification.replace(
        '<!-- BUNDLE-VERSION-PENDING -->',
        'The signed bundle reports `CFBundleShortVersionString=' + info['CFBundleShortVersionString'] + '` and `CFBundleVersion=' + BUILD + '`. IPA/dSYM filenames use the same bundle version.')
    verification = verification.replace(
        'The parent is preparing the signed build; artifact verification and physical-device acceptance are pending.',
        'The parent built and signed the app; artifact verification passes and physical-device acceptance is pending.')
    (base / 'VERIFICATION.md').write_text(verification.replace(marker, result))
    records = list(evidence) + ['record-artifact.py', 'VERIFICATION.md', 'build-manifest.json',
        'source-changes.patch', 'signed-entitlements.plist', 'entitlements-read.log',
        'codesign-verification.txt', 'app-files.sha256', 'dsym-files.sha256', 'build-summary.log']
    records += [str(p.relative_to(base)) for p in (base / 'source-snapshot').rglob('*') if p.is_file()]
    records += [a['name'] for a in archives]
    (base / 'SHA256SUMS').write_text(''.join(sha(base / name) + '  ' + name + '\n' for name in sorted(records)))
    print(json.dumps({'build': BUILD, 'source': COMMIT, 'uuid_records': uuids,
                      'archives': archives, 'device_status': 'pending',
                      'repository_copy': 'not modified'}, indent=2))


if __name__ == '__main__':
    main()
