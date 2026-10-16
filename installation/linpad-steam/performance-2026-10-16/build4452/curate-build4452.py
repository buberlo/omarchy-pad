from pathlib import Path
import hashlib,json,shutil,subprocess
base=Path(__file__).resolve().parent;work=base.parents[1];repo=work/'LinPad';out=repo/'installation/linpad-steam/performance-2026-10-16/build4452'
out.mkdir(exist_ok=True)
for name in ['build-manifest.json','source-changes.patch','source-commit.txt','build-local.sh','record-artifact.py','artifact-verification.log','signing.log','signed-entitlements.plist','install.json','install.log','terminate-predecessor.log','curate-build4452.py']:
 shutil.copy2(base/name,out/name)
shutil.copytree(base/'source-snapshot',out/'source-snapshot',dirs_exist_ok=True)
for name in ['lrcpc2-device4452.txt','smc4452-device-pass.txt']:
 shutil.copy2(work/'performance/reports'/name,out/name)
for name in ['prepare4452','smc4452-probe','hollow4452-egl','hollow4452-short','hollow4452-frame-capture','hollow4452-menu-cadence','ss4452-probe']:
 p=out/'device-scripts'/name;p.parent.mkdir(exist_ok=True);shutil.copy2(work/'performance/serve'/name,p)
text=(base/'device-console.log').read_text(errors='replace');lines=[l for l in text.splitlines() if '[fast mode]' in l or 'native JIT on:' in l or '[boot] case_sensitive' in l]
(out/'boot-jit-excerpt.txt').write_text('\n'.join(lines)+'\n')
shutil.copy2(work.parent/'outputs/linpad4452-device-tests.png',out/'device-tests.png')
m=json.loads((base/'build-manifest.json').read_text())
(out/'SHA256SUMS').write_text(''.join(a['sha256']+'  '+a['name']+'\n' for a in m['archives']))
verification=f'''# Build4452: installed and physical low-level checks passed

Source commit `{m['source_commit']}` adds native LRCPC2 LDAPUR/STLUR translation. This replaces the hot gadget fallback measured on4451 with translated ordinary scalar memory operations and DMB ISH barriers. The new release-store path commits split-page writes immediately; default FEX feature detection is retained. No production Steam-launcher or FEX binary change is included.

The signed LinPad1.4.0 app has bundle `com.konradkern.linpad`, both approved memory entitlements and matching executable/dSYM UUID `{m['app_and_dsym_uuid']}`. Signature, full ZIP CRC and every archived member were verified. The root filesystem archive is unchanged from4450; existing installed Linux/user data remain separate. Artifact source, hashes and local rebuild/recorder are included. An initial sandbox-only codesign trust check could not access the trust chain; the same already signed app passed the authorized host check. That sandbox diagnostic remains local and does not represent an invalid app.

Physical device00008112-000924EE14D9A01E installed the app at its new bundle path in install.json. The prior own host21692 was deliberately terminated for the update; host21791 booted4452, and StikDebug enabled the512MiB native JIT arena. The previous-close notifications follow that manual termination and are not evidence of a4452 crash.

At14:17:37–38UTC both existing SMC probes passed; the fallback-rewrite probe reports200 checks. At14:17:38UTC the new real ARM64 guest probe passed19151 checks, zero failures and exit0:8190 width/sign/register/address cases, exact fault addresses and offending PCs, reserved SIGILL,200 LRCPC2 code rewrites and10000 two-thread message exchanges. The screenshot and unmodified textual reports preserve this device evidence. These intentional access faults are probe cases, not an observed app/game crash.

Local actual-translator ASan/UBSan controls passed393224 cases and4454512 assertions. The same guest test passed tier1 inline, tier1 compact and tier2. The unchanged gadget-only baseline retains9 failing strict fault-PC assertions while values, signal addresses, SMC and threading pass; it reports the block start36bytes before the memory instruction. See ../lrcpc2-native for the unchanged local evidence and test sources.

**No Hollow Knight performance result is claimed for4452 yet.** The last matched menu measurement is4451's212 compositor deliveries/30s (7.0667/s at659×540). Smooth audio, normal playable gameplay and the final Steam-to-game production launch remain unqualified. The rejected `FEX_HOSTFEATURES=disablelrcpc2` control stays off. Game/runtime files, Steam authentication, signing private keys and pairing material are not uploaded.

Next: enable a fresh compositor frame log before starting a new game; run the unchanged default FEX/Wayland/EGL/Venus route, collect bounded fallback counters, compare the same foreground menu, then verify actual movement/combat and audio. Only integrate the route into Steam after those gates pass. The native patch does not alter the installed Steam compatibility tool.
'''
(out/'VERIFICATION.md').write_text(verification);(base/'release-notes.md').write_text(verification)
# Keep initial artifact manifest immutable; physical gates are a separate record.
(out/'device-validation.json').write_text(json.dumps({'build':4452,'source_commit':m['source_commit'],'installed':True,'host_pid':21791,'jit_mebibytes':512,'smc_pass':True,'lrcpc2_checks':19151,'lrcpc2_failures':0,'lrcpc2_exit':0,'gameplay_verified':False,'game_performance_verified':False},indent=2)+'\n')
(out/'source-evidence.json').write_text(json.dumps({str(p.relative_to(work.parent)):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in [base/'build.log',base/'device-console.log',base/'artifact-verification-sandbox-trust.log']},indent=2)+'\n')
(out/'REPOSITORY-SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(out))+'\n' for p in sorted(out.rglob('*')) if p.is_file() and p.name!='REPOSITORY-SHA256SUMS'))
print('Curated',len(list(out.rglob('*'))),'entries',out)
