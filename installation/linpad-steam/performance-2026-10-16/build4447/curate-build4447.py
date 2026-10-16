#!/usr/bin/env python3
"""Curate existing build4447 artifacts and completed reports; no device/network/Git mutations.

Does not rerun or overwrite record-artifact.py or its immutable initial manifest.
Large IPA/dSYM archives stay in this scratch build directory, never copied to Git.
"""
from pathlib import Path
import datetime,hashlib,json,plistlib,re,shutil,subprocess
BASE=Path(__file__).resolve().parent
WORK=BASE.parents[1]
REPO=WORK/'LinPad'
OUT=BASE/'curated'
DEST=REPO/'installation/linpad-steam/performance-2026-10-16/build4447'
OUT.mkdir(exist_ok=True)
records=[]
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def save(name,data):
 p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True)
 p.write_bytes(data if isinstance(data,bytes) else data.encode())
 return p
def cp(src,name=None):
 src=Path(src);name=name or src.name
 dst=save(name,src.read_bytes());records.append({'source':str(src.relative_to(WORK.parent)),'destination':name,'source_sha256':sha(src),'source_bytes':src.stat().st_size,'mode':'byte-identical','archived_sha256':sha(dst)})
 return dst
def excerpt(src,name,predicate):
 src=Path(src);data=src.read_bytes();lines=data.decode(errors='replace').splitlines()
 chosen=[f'{i+1}: {line}' for i,line in enumerate(lines) if predicate(line,i)]
 dst=save(name,'\n'.join(chosen)+'\n');records.append({'source':str(src.relative_to(WORK.parent)),'destination':name,'source_sha256':hashlib.sha256(data).hexdigest(),'source_bytes':len(data),'mode':'numbered selected lines; full raw source remains local','archived_sha256':sha(dst)})
 return '\n'.join(lines)
initial=json.loads((BASE/'build-manifest.json').read_text())
assert initial['build']=='4447' and initial['source_commit']==(BASE/'source-commit.txt').read_text().strip()
# Recheck small identity-bearing files; original record already verifies ZIP CRC and every member.
app=BASE/'Payload/iSH ARM64.app';info=plistlib.loads((app/'Info.plist').read_bytes());exe=app/info['CFBundleExecutable'];dwarf=BASE/'iSH ARM64.app.dSYM/Contents/Resources/DWARF'/info['CFBundleExecutable']
assert info['CFBundleVersion']=='4447'
assert sha(exe)==initial['executable_sha256'] and sha(dwarf)==initial['dwarf_sha256']
for row in initial['archives']:assert (BASE/row['name']).stat().st_size==row['bytes']
for row in initial['source_files']:
 p=BASE/'source-snapshot'/row['path'];assert sha(p)==row['sha256'];cp(p,'source-snapshot/'+row['path'])
 cp_blob=subprocess.check_output(['git','show',initial['source_commit']+':'+row['path']],cwd=REPO)
 assert hashlib.sha256(cp_blob).hexdigest()==row['sha256']
for n in ['source-commit.txt','source-changes.patch','build-local.sh','record-artifact.py','artifact-verification.log','signed-entitlements.plist']:
 cp(BASE/n)
cp(BASE/'build-manifest.json','initial-build-manifest.json')
cp(Path(__file__))
excerpt(BASE/'build.log','build-summary.log',lambda s,i:i<3 or '** BUILD SUCCEEDED **' in s or 'ISH_JIT_BUILD = enabled' in s or 'jit            : enabled' in s)
excerpt(BASE/'sign.log','signing-excerpt.txt',lambda s,i:s.startswith('SIGNED AND VERIFIED'))
install=json.loads((BASE/'install.json').read_text());assert install['info']['outcome']=='success'
apps=install['result']['installedApplications'];assert any(a['bundleID']==initial['bundle_id'] for a in apps)
save('device-install-evidence.json',json.dumps({'outcome':'success','command_type':install['info']['commandType'],'installed_bundle_ids':[a['bundleID'] for a in apps],'raw_install_sha256':sha(BASE/'install.json'),'raw_install_bytes':(BASE/'install.json').stat().st_size,'raw_identifiers_and_container_paths_excluded':True},indent=2)+'\n')
console=excerpt(BASE/'device-console.log','device-boot-excerpt.txt',lambda s,i:s.startswith('Launched application with ') or '[boot]' in s or s.startswith('ish: native JIT on:'))
launched='Launched application with '+initial['bundle_id'] in console
jit='ish: native JIT on:' in console and '512 MB code arena' in console
boot='[boot] boot_seconds=' in console
reports=WORK/'performance/reports';serve=WORK/'performance/serve'
for n in ['maps4447-probe','mono4447-probe']:
 cp(serve/n,'device-scripts/'+n)
# Only finalized uniquely named reports become evidence; partial runtime /tmp files are never read.
physical={'installed':True,'launched':launched,'native_jit_512MiB':jit,'boot_completed':boot,'maps_probe':'pending','mono_probe':'pending','game_rendered_frame':'pending','gameplay':'pending','performance':'pending'}
for name,kind in [('maps4447-device-pass.txt','maps'),('mono4447-device-pass.txt','mono')]:
 p=reports/name
 if not p.exists():continue
 text=p.read_text();assert len(text)<131072
 if kind=='maps':
  assert 'PROC_MAPS_BOUNDARY checks=10 failures=0' in text and 'MAPS_DEVICE_EXIT=0' in text
  physical['maps_probe']={'status':'passed','checks':10,'failures':0,'exit':0,'source_sha256':sha(p),'evidence':name}
  excerpt(p,name,lambda s,i:s.startswith(('Fri Oct','PASS ','PROC_MAPS_BOUNDARY','MAPS_DEVICE_EXIT=','/tmp/proc-maps-boundary-probe:','footprint_mb ','allowance_mb ','headroom_mb ','kills ')))
 else:
  required=['STACK_LIMITS_EXIT=0','MONO_DEVICE_EXIT flags=0 code=0','MONO_DEVICE_EXIT flags=3221 code=0','MONO_DEVICE_SUITE_EXIT=0','MONO_DEVICE_CONFIG SDL=wayland FEX_MULTIBLOCK=unset LD_PRELOAD=unset']
  assert all(v in text for v in required)
  physical['mono_probe']={'status':'passed','default_FEX':True,'LD_PRELOAD':'unset','SDL_backend':'wayland','SDL_flags':['0','0x3221'],'stack_exit':0,'both_mono_init_exit':0,'suite_exit':0,'source_sha256':sha(p),'evidence':name}
  excerpt(p,name,lambda s,i:s.startswith(('Fri Oct','STACK_LIMITS','MONO_','/opt/','/root/')))
manifest=dict(initial)
manifest['curated_at_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
manifest['physical']=physical
manifest['device_validation']='See physical gates and uniquely named report excerpts. Game rendering/playability/performance remain pending.'
manifest['initial_build_manifest_sha256']=sha(BASE/'build-manifest.json')
manifest['scope']='Signed archives verified separately; curated repo excludes IPA/dSYM bytes, raw process/console logs, device identifiers, provisioning-profile bytes and proprietary game inputs.'
save('build-manifest.json',json.dumps(manifest,indent=2)+'\n')
save('symbols.json',json.dumps({'executable_sha256':initial['executable_sha256'],'dwarf_sha256':initial['dwarf_sha256'],'matching_uuid':initial['app_and_dsym_uuid'],'architecture':'arm64'},indent=2)+'\n')
archive_sums=''.join(r['sha256']+'  '+r['name']+'\n' for r in initial['archives'])
save('SHA256SUMS',archive_sums)
# Make release companion files available beside the archives, without changing originals.
(BASE/'SHA256SUMS').write_text(archive_sums)
map_text='physical native map-boundary regression passes 10/10' if physical['maps_probe']!='pending' else 'physical map-boundary regression is pending'
mono_text='physical default-FEX Mono initialization passes with no SDL and with the real Wayland SDL backend' if physical['mono_probe']!='pending' else 'physical default-FEX Mono validation is pending'
notes=f'''LinPad 1.4.0 build 4447 fixes incorrect `/proc/self/maps` boundaries that caused glibc to infer a false main-stack range and Mono to protect live FEX metadata.

Built and signed from `{initial['source_commit']}`; signed IPA and matching dSYM passed CRC/content verification, matching UUID `{initial['app_and_dsym_uuid']}`. Both approved memory entitlements remain enabled. Installed and booted on the physical iPad with 512 MiB native JIT; {map_text}; {mono_text}.

Game rendering, playability and steady-state performance remain pending. Installed FEX/Mesa/EGL/Steam/game data are separate from the IPA. This is a development checkpoint.
'''
save('release-notes.md',notes);(BASE/'release-notes.md').write_text(notes)
verification=f'''# Build 4447 verification

{notes}
The immutable initial artifact record is copied as `initial-build-manifest.json`; the original `record-artifact.py` was not modified or rerun. `curate-build4447.py` reads completed uniquely named reports and updates this separate curated record. It performs no device, network, staging or commit actions.

The executable and dSYM DWARF hashes were rechecked during curation. The original recorder checked every ZIP member against its source bundle and all CRCs; its log and exact archive hashes are preserved. The curation does not claim a second full ZIP traversal.

`SHA256SUMS` covers the large IPA and dSYM release assets, retained outside Git. `REPOSITORY-SHA256SUMS` covers this small curated directory. `source-evidence.json` fingerprints the original logs and identifies narrowed excerpts. Raw device IDs, process dumps and console logs remain local. Source snapshots match committed blobs.

Matched local cause/repair controls are in the adjacent `../mono-stack-repair` directory. Physical probes are separate gates; a completed probe does not establish gameplay or frame-rate quality. Release upload/digest verification has not yet been recorded here.
'''
save('VERIFICATION.md',verification)
save('source-evidence.json',json.dumps({'files':records},indent=2)+'\n')
# Include the latest script bytes as a reproducible companion.
save('REPOSITORY-SHA256SUMS',''.join(sha(f)+'  '+str(f.relative_to(OUT))+'\n' for f in sorted(OUT.rglob('*')) if f.is_file() and f.name!='REPOSITORY-SHA256SUMS'))
shutil.copytree(OUT,DEST,dirs_exist_ok=True)
for root in [OUT,DEST]:
 for line in (root/'REPOSITORY-SHA256SUMS').read_text().splitlines():
  h,n=line.split('  ',1);assert sha(root/n)==h,n
 for f in root.rglob('*.json'):json.loads(f.read_text())
print(json.dumps({'curated_files':sum(f.is_file() for f in OUT.rglob('*')),'repo_hash':sha(OUT/'REPOSITORY-SHA256SUMS'),'physical':physical},indent=2))
