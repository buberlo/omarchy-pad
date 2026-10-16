import json,fcntl,datetime,pathlib,sys,os,tempfile,uuid
p=pathlib.Path('/Users/konrad/Documents/ChatGPT/madeira/installation/ipad-access.json');owner='linpad';now=datetime.datetime.now(datetime.timezone.utc).isoformat()
with p.with_suffix('.lock').open('a') as f:
 fcntl.flock(f,fcntl.LOCK_EX);s=json.loads(p.read_text())
 if sys.argv[1]=='acquire':
  assert s.get('owner') is None,'Device still owned by '+str(s.get('owner'))
  assert not s.get('deviceWorkPaused'),'Device work paused'
  token=str(uuid.uuid4());s.update(owner=owner,status='linpad-install-active',activePhase='User-requested bounded LinPad 3D Vulkan/GPU smoke test',linpadLeaseToken=token)
  s.setdefault('allowedOwners',[])
  if owner not in s['allowedOwners']:s['allowedOwners'].append(owner)
  s['linpadAuthorization']={'instruction':'install this on my ipad. sign it with my dev account; Give LinPad priority','recordedAt':now}
  pathlib.Path('work/linpad-lease-token').write_text(token)
 else:
  assert s.get('owner')==owner and s.get('linpadLeaseToken')==pathlib.Path('work/linpad-lease-token').read_text()
  s.update(owner=None,lastOwner=owner,status='linpad-install-released');s.pop('activePhase',None);s.pop('linpadLeaseToken',None)
 s['updatedAt']=now
 fd,temp=tempfile.mkstemp(prefix='.linpad-',dir=p.parent)
 with os.fdopen(fd,'w') as out:json.dump(s,out,indent=2);out.write('\n')
 os.replace(temp,p);print(s['status'])
