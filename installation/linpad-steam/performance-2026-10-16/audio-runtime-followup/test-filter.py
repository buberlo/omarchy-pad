from pathlib import Path
import subprocess
script=Path(__file__).with_name('filter-game-sink-inputs.awk')
fixture='''Sink Input #7
    Sink: 2
    Corked: no
    Mute: no
    Buffer Latency: 100 usec
    Sink Latency: 200 usec
    Properties:
        application.process.id = "366"
        application.name = "SECRET_GAME_NAME"
        auth.cookie = "SECRET_COOKIE"
Sink Input #8
    Sink: 2
    Corked: yes
    application.process.id = "999"
    media.name = "SECRET_OTHER_STREAM"
QUERY_EXIT=0
'''
r=subprocess.run(['awk','-v','target=366','-f',str(script)],input=fixture,text=True,capture_output=True,check=True)
assert 'SECRET' not in r.stdout and 'index=8' not in r.stdout
assert 'GAME_STREAM index=7 sink=2 pid=366 corked=no mute=no buffer_usec=100 sink_usec=200' in r.stdout
assert 'GAME_STREAMS_MATCHING_PID=1' in r.stdout and 'QUERY_EXIT=0' in r.stdout
r=subprocess.run(['awk','-v','target=365','-f',str(script)],input=fixture,text=True,capture_output=True,check=True)
assert 'GAME_STREAM index=' not in r.stdout and 'GAME_STREAMS_MATCHING_PID=0' in r.stdout
r=subprocess.run(['awk','-v','target=366','-f',str(script)],input=fixture.replace('"366"','"366 SECRET"'),text=True,capture_output=True,check=True)
assert 'SECRET' not in r.stdout and 'GAME_STREAMS_MATCHING_PID=0' in r.stdout
print('PASS: 3 parser fixtures; only requested numeric PID and whitelisted stream state emitted')
