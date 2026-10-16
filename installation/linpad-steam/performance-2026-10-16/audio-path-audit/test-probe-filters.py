#!/usr/bin/env python3
"""Exercise the shipped probe's awk allowlists with synthetic sensitive fields."""
from pathlib import Path
import re, subprocess, json
root=Path(__file__).resolve().parent
s=(root/'audio-path-probe').read_text()
programs=re.findall(r'''awk -v file="\$p" '(.*?)' "\$p"''',s,re.S)
modules=re.findall(r'''awk '(.*?)' "\$p"''',s,re.S)
assert len(programs)==2 and len(modules)==1
secret='SYNTHETIC_SECRET_MUST_NOT_APPEAR'
fixtures=[
 (programs[0], 'default-server = unix:/tmp/ishaudio/native\nautospawn = no\nenable-shm = no\ncookie-file = /private/'+secret+'\npassword = '+secret+'\n# default-server = '+secret+'\ndefault-server = unix:/tmp/socket password='+secret+'\n', ['default-server=unix:/tmp/ishaudio/native','autospawn=no','enable-shm=no']),
 (modules[0], 'load-module module-native-protocol-unix auth-cookie=/private/'+secret+' socket=/tmp/ishaudio/native\nload-module module-pipe-sink file=/tmp/ishaudio/pcm sink_name=ishaudio auth-cookie='+secret+'\nset-default-sink ishaudio\n', ['socket=/tmp/ishaudio/native','file=/tmp/ishaudio/pcm','set-default-sink ishaudio']),
 (programs[1], 'pcm.!default { type pulse }\nctl.!default { type pulse }\npassword '+secret+'\n', ['pcm.!default=pulse','ctl.!default=pulse']),
]
for code,fixture,expected in fixtures:
 result=subprocess.run(['awk','-v','file=/synthetic-config',code],input=fixture,text=True,capture_output=True,check=True)
 assert secret not in result.stdout and 'auth-cookie' not in result.stdout and 'cookie-file' not in result.stdout
 for item in expected: assert item in result.stdout,item
subprocess.run(['sh','-n',str(root/'audio-path-probe')],check=True)
print(json.dumps({'shell_syntax':'PASS','actual_awk_whitelist_tests':3,'sensitive_fixture_output':'absent','device_execution':False}))
