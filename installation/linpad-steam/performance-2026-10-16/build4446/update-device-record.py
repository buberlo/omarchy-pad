#!/usr/bin/env python3
"""Record the validated 4446 boot/SDL checkpoint; copy only approved light files.

No build, signing, install, device interaction, Git mutation or publication.
The original console/install/process logs and packaged binaries stay local.
"""
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil

base = Path(__file__).resolve().parent
workspace = base.parents[2]
repo = workspace / 'work/LinPad'
dest = repo / 'installation/linpad-steam/performance-2026-10-16/build4446'
reports = base.parent / 'reports'

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

manifest = json.loads((base / 'build-manifest.json').read_text())
assert manifest['source_head'] == '72e1eef8b3125b39e7c852abf2d16bf7a1f82029'
assert manifest['version'] == '1.4.0' and manifest['build'] == '4446'
assert not manifest['device_validation']['gameplay_verified'], 'Preserve newer qualification'
install = json.loads((base / 'install.json').read_text())
assert install['info']['outcome'] == 'success'
assert any(a['bundleID'] == manifest['bundle_id'] for a in install['result']['installedApplications'])
now = datetime.datetime.now(datetime.timezone.utc).isoformat()

netlink_raw = (reports / 'netlink4446-device-pass.txt').read_text()
netlink_lines = netlink_raw.splitlines()
end = netlink_lines.index('NETLINK_EXIT=0')
netlink_curated = '\n'.join(netlink_lines[:end + 1]) + '\n'
assert 'NETLINK_UEVENT_PASS failures=0' in netlink_curated
assert len(re.findall(r'^.* PASS errno=', netlink_curated, re.MULTILINE)) == 35
(base / 'netlink4446-device-pass.txt').write_text(netlink_curated)

unity = reports / 'unity-sdl-v2-4446-after.txt'
unity_text = unity.read_text()
for flags in ['1000', '200', '21', '3221']:
    assert f'UNITY_SDL_INIT_RESULT flags=0x{flags} result=0' in unity_text
    assert f'EMBEDDED_SDL_EXIT={flags}:0' in unity_text
assert unity_text.count('UNITY_SDL_NUM_DISPLAYS 1') == 2
assert 'DIRECT_UDEV_EXIT=6' in unity_text and 'enable=-22' in unity_text
shutil.copy2(unity, base / unity.name)
shutil.copy2(base.parent / 'serve/unity-sdl-after-probe', base / 'unity-sdl-after-probe')

console_bytes = (base / 'device-console.log').read_bytes()
console_lines = console_bytes.decode(errors='replace').splitlines()
boot_lines = [line for line in console_lines if line.startswith('Launched application with ') or
              'native JIT on:' in line or '[boot]' in line]
assert any('512 MB code arena' in line for line in boot_lines)
assert any('fast_mode=on' in line for line in boot_lines)
assert any('boot_seconds=0.18' in line for line in boot_lines)
(base / 'device-boot-excerpt.txt').write_text('\n'.join(boot_lines) + '\n')
install_summary = {
    'outcome': 'success', 'bundle_id': manifest['bundle_id'],
    'command_type': install['info']['commandType'],
    'source_file': 'install.json', 'source_sha256': sha(base / 'install.json'),
    'curation': 'Device serial, container identifiers and command environment omitted.'
}
(base / 'device-install-evidence.json').write_text(json.dumps(install_summary, indent=2) + '\n')

device_evidence = {
    'recorded_at_utc': now,
    'acceptance_scope': 'Physical boot and netlink/embedded-SDL initialization checkpoint only',
    'install': install_summary,
    'console_source': {
        'file': 'device-console.log',
        'snapshot_bytes': len(console_bytes),
        'snapshot_sha256': hashlib.sha256(console_bytes).hexdigest(),
        'note': 'The live console continues growing; hash applies to the first snapshot_bytes bytes at this checkpoint. Only boot/JIT lines are archived.'
    },
    'report_sources': [
        {'file': '../reports/netlink4446-device-pass.txt',
         'sha256': sha(reports / 'netlink4446-device-pass.txt'),
         'curation': 'Only timestamp, diagnostic hash and native test result through NETLINK_EXIT retained; process list, memory and filesystem inventory omitted.'},
        {'file': '../reports/unity-sdl-v2-4446-after.txt', 'sha256': sha(unity),
         'curation': 'Copied unchanged; contains diagnostic results only.'}
    ],
    'unity_player_sha256': '12ce7af93c41b13b3e7d9b75b591336cc56c9ce113714143fd897cc1dcf00e33',
    'embedded_probe_sha256': '42ea48be641a1211f5291b224c7cc34585b6a04a8323b2855c33c03d28bd30d6',
    'native_probe_sha256': '2702a958e557037da23e944629f2ec74eb658d5fc506b02251ea13ac8c1b06ee',
    'libudev_boundary': 'Monitor construction succeeds. BPF filter enable remains unsupported (-22), so direct UDEV diagnostic exit6 is expected and retained. All four exact Unity SDL initialization calls return0.'
}
(base / 'device-validation-provenance.json').write_text(json.dumps(device_evidence, indent=2) + '\n')

manifest.update(installed=True, launched=True, device_accepted=True,
                device_acceptance_scope=device_evidence['acceptance_scope'],
                device_recorded_at_utc=now,
                physical_device_validation='PASS for boot with native JIT512MiB, all35 native netlink cases, and exact embedded Unity SDL initialization including full0x3221 with one display. GPU/game frame, gameplay and steady-state performance remain pending.',
                device_evidence='device-validation-provenance.json')
manifest['device_validation'].update(
    boot={'result': 'pass', 'reported_boot_seconds': 0.18, 'native_jit': True,
          'jit_code_arena_mib': 512, 'evidence': 'device-boot-excerpt.txt'},
    native_netlink_probe={'result': 'pass', 'case_count': 35, 'exit_code': 0,
                          'evidence': 'netlink4446-device-pass.txt'},
    exact_unity_sdl_init={'result': 'pass', 'flags_tested': ['0x1000','0x200','0x21','0x3221'],
                          'video_display_count': 1, 'combined_display_count': 1,
                          'evidence': 'unity-sdl-v2-4446-after.txt'},
    gpu_test='pending', rendered_game_frame_verified=False, gameplay_verified=False,
    steady_state_performance_qualified=False)
manifest['limitations'] = [x for x in manifest['limitations'] if not x.startswith('512 MiB JIT')]
manifest['publication_status'] = 'Prepared local checkpoint; no upload or release by this updater'
(base / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

verification = '''# LinPad 1.4.0 build 4446

Build 4446 is installed and launched on the physical iPad. Boot and the netlink/Unity SDL initialization fix pass. GPU presentation on this build, a rendered Hollow Knight frame, gameplay and steady-state performance remain pending; earlier build 4445 GPU results are historical controls.

Source: `72e1eef8b3125b39e7c852abf2d16bf7a1f82029`. The actual signed bundle reports version `1.4.0`, build `4446`. App and matching dSYM UUID: `7BF394C6-2F81-36E0-A997-90220832DB68`.

## Physical validation

- App installation succeeded; `device-install-evidence.json` preserves the result and raw-record hash without device/container identifiers.
- Native JIT starts with a 512 MiB code arena, fast mode on. The app's boot marker reports 0.18 seconds. This marker measures runtime boot, not the preceding manual JIT enablement or game startup.
- All 35 native netlink regression checks pass, including 101.1 ms idle poll, unique monitor ports, full group-mask readback, duplicate descriptor lifetime and existing route replies.
- Exact Unity embedded SDL haptic, joystick, timer/video and combined `0x3221` initialization all return 0. Video-only and combined initialization each retain one display. The identical v2 probe failed combined initialization on build 4445; see the adjacent `unity-sdl-repair` evidence archive.

Real libudev monitor creation succeeds. Its optional BPF filter enable still returns -22, and the direct UDEV diagnostic consequently exits 6. Unity ignores that later operation and all four SDL initialization calls pass. Residual SDL error strings are not failures when the associated call returns 0. No hotplug producer, BPF filter or dynamic membership support is claimed.

## Artifact and source verification

The local recorder verified build completion, deep/strict developer signature, both approved memory entitlements, matching app/dSYM UUIDs, complete ZIP CRCs and every packaged regular file against its copied bundle. `build-manifest.json` records executable, dSYM, rootfs and package SHA256 values. The root archive matches build 4445. Six committed source snapshots, their patch and the focused host regressions are included.

The fix adds an idle `NETLINK_KOBJECT_UEVENT` endpoint for libudev, retaining nonblocking/poll behavior, credentials-option state, group masks and port lifetime. Unsupported sends cannot be interpreted as route requests. The final local native probe also passes all 35 checks. Local embedded SDL after-fix results preceded the final group-mask-only correction; physical build 4446 now independently passes the exact combined initialization.

FEX, Mesa 26, EGL 25, Steam and installed games remain separate guest data, not a complete filesystem export in this IPA. The repository copy contains curated evidence and source text only. IPA, dSYM binaries, Payload, full device console/process inventories and credentials are excluded. `SHA256SUMS` indexes the local checkpoint including separate release assets; `REPOSITORY-SHA256SUMS` verifies only files included in the repository copy.

This is a boot/subsystem checkpoint. No rendered-game-frame, gameplay or FPS claim is made.
'''
(base / 'VERIFICATION.md').write_text(verification)
(base / 'release-notes.md').write_text('''LinPad 1.4.0 build 4446 fixes Unity SDL startup rollback by adding an idle netlink uevent monitor endpoint. On the physical iPad, native JIT uses a 512 MiB arena, boot succeeds, all 35 native regression checks pass, and the exact game SDL initialization retains one display.

GPU presentation on this build, Hollow Knight rendering/playability and steady-state performance are still pending. Libudev BPF filtering and actual hotplug delivery remain unsupported; this checkpoint does not claim them.

Source: `72e1eef8b3125b39e7c852abf2d16bf7a1f82029`. Signed IPA and matching dSYM ZIP passed CRC and content verification. Both approved memory entitlements remain enabled. Installed FEX/Mesa/EGL/game data are separate from the IPA.
''')
summary = (base / 'build-summary.log').read_text()
summary = summary.replace('Device acceptance pending.', 'Physical boot/JIT512, native35-case netlink and exact Unity SDL initialization PASS. GPU/gameplay/performance pending.')
(base / 'build-summary.log').write_text(summary)

# Preserve the original release-asset index and extend it with curated records.
indexed = {line.split('  ',1)[1] for line in (base/'SHA256SUMS').read_text().splitlines() if line}
indexed.update(['device-boot-excerpt.txt', 'device-install-evidence.json',
                'device-validation-provenance.json', 'netlink4446-device-pass.txt',
                'unity-sdl-v2-4446-after.txt', 'unity-sdl-after-probe',
                'release-notes.md', 'update-device-record.py'])
(base/'SHA256SUMS').write_text(''.join(sha(base/name)+'  '+name+'\n' for name in sorted(indexed)))

# Exact allowlist: no recursive copy of the build directory or live reports.
excluded = {a['name'] for a in manifest['archives']}
copy_names = indexed - excluded
copy_names.add('SHA256SUMS')
dest.mkdir(parents=True, exist_ok=True)
for name in sorted(copy_names):
    target = dest / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(base/name, target)
repo_files = sorted(p for p in dest.rglob('*') if p.is_file() and p.name != 'REPOSITORY-SHA256SUMS')
assert all(p.suffix not in {'.ipa','.zip','.so'} for p in repo_files)
assert all('Payload' not in p.parts and '__pycache__' not in p.parts for p in repo_files)
(dest/'REPOSITORY-SHA256SUMS').write_text(''.join(sha(p)+'  '+str(p.relative_to(dest))+'\n' for p in repo_files))
print(json.dumps({'build':4446,'installed':True,'boot':'pass','native_cases':35,
                  'combined_sdl_displays':1,'gameplay':'pending','repository_files':len(repo_files)+1,
                  'repository_bytes':sum(p.stat().st_size for p in dest.rglob('*') if p.is_file()),
                  'repository_checksum_sha256':sha(dest/'REPOSITORY-SHA256SUMS')},indent=2))
