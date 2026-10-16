from pathlib import Path
import plistlib, subprocess, shutil, hashlib, datetime, json, sys
base=Path(__file__).resolve().parent
app=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else base/'signed/Payload/iSH ARM64.app'
profile=base/'signing/build/Build/Products/Debug-iphoneos/LinPad.app/embedded.mobileprovision'
p=plistlib.loads(subprocess.check_output(['security','cms','-D','-i',str(profile)]))
identity='0B49D6C4CDB9B6FA1D15530AF43AE577037F93A5'
assert identity.lower() in [hashlib.sha1(c).hexdigest() for c in p['DeveloperCertificates']]
assert '00008112-000924EE14D9A01E' in p['ProvisionedDevices']
assert p['ExpirationDate']>datetime.datetime.now()
ent={'application-identifier':'K5TW9AU245.com.konradkern.linpad','com.apple.developer.team-identifier':'K5TW9AU245','keychain-access-groups':['K5TW9AU245.com.konradkern.linpad'],'get-task-allow':True,'com.apple.security.application-groups':['group.com.konradkern.linpad'],'com.apple.developer.user-fonts':['app-usage']}
ent['com.apple.developer.kernel.increased-memory-limit']=True
ent['com.apple.developer.kernel.extended-virtual-addressing']=True
for k,v in ent.items():
 assert k in p['Entitlements'],k
 if isinstance(v,list):assert all(x in p['Entitlements'][k] or (k=='keychain-access-groups' and 'K5TW9AU245.*' in p['Entitlements'][k]) for x in v),k
 else:assert p['Entitlements'][k]==v,k
info=plistlib.loads((app/'Info.plist').read_bytes());info['CFBundleIdentifier']='com.konradkern.linpad';(app/'Info.plist').write_bytes(plistlib.dumps(info))
shutil.copy2(profile,app/'embedded.mobileprovision')
ep=base/'linpad-entitlements.plist';ep.write_bytes(plistlib.dumps(ent))
magic={bytes.fromhex(x) for x in ['cffaedfe','cefaedfe','feedfacf','feedface','cafebabe','bebafeca','cafebabf','bfbafeca']}
for f in app.rglob('*'):
 if not f.is_file() or f.name==info['CFBundleExecutable']:continue
 with f.open('rb') as stream:m=stream.read(4)
 if m in magic:
  subprocess.run(['codesign','--force','--sign',identity,'--timestamp=none',str(f)],check=True)
subprocess.run(['codesign','--force','--sign',identity,'--timestamp=none','--entitlements',str(ep),'--generate-entitlement-der',str(app)],check=True)
subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
print('SIGNED AND VERIFIED',app,'profile expires',p['ExpirationDate'])
