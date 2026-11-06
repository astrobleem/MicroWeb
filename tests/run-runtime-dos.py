#!/usr/bin/env python3
"""Optional DOSBox-X regression: executable-relative DAT files and IRQ cleanup.
Requires WATCOM, DOSBOX_X, and the DOS build. BASELINE_EXE enables negative tests.
"""
from pathlib import Path
import os
import shutil
import signal
import subprocess
root = Path(__file__).resolve().parent.parent
work = root / 'build/runtime-dos-test'
work.mkdir(parents=True, exist_ok=True)
watcom = Path(os.environ['WATCOM']).resolve()
bin_dir = watcom / ('binl64' if (watcom/'binl64/wcl').exists() else 'binl')
build_env = dict(os.environ, WATCOM=str(watcom), LIBDOS=str(watcom/'lib286/dos')+os.pathsep+str(watcom/'lib286'), PATH=str(bin_dir)+os.pathsep+os.environ['PATH'], INCLUDE=str(watcom/'h'))
subprocess.run([str(bin_dir/'wcl'), '-zq', '-0', '-s', '-bt=dos', '-ms', '-fe=RTEXIT.EXE',
                str(root/'tests/runtime_exit.c')], cwd=work, env=build_env, check=True)
adapter=(root/'tests/wheel_sdl.c').read_text().replace('6.0 + stage * 2.0', '(getenv("MW_TEST_DELAY") ? atof(getenv("MW_TEST_DELAY")) : 6.0) + stage * 2.0')
(work/'events.c').write_text(adapter)
subprocess.run([os.getenv('CC','cc'), '-shared', '-fPIC', str(work/'events.c'),
                '-ldl', '-o', str(work/'events.so')], check=True)
def run(label, executable, in_app=False, via_path=False, url=None):
    case=work/label; case.mkdir(exist_ok=True)
    guest=case/'guest'; guest.mkdir(exist_ok=True)
    app=guest/'APP'; app.mkdir(exist_ok=True)
    shutil.copyfile(executable,app/'MICROWEB.EXE')
    shutil.copyfile(work/'RTEXIT.EXE',guest/'RTEXIT.EXE')
    for name in ('Default.dat','LowRes.dat','CGA.dat','EGA.dat'):
        shutil.copyfile(root/name,app/name)
    (guest/'LOCAL.HTM').write_text('<html><head><title>Runtime regression</title></head><body><h1>Caller directory page</h1><p>Executable-relative assets loaded successfully.</p></body></html>')
    if in_app: shutil.copyfile(guest/'LOCAL.HTM',app/'LOCAL.HTM')
    result=(app if in_app else guest)/'RESULT.TXT'
    if result.exists(): result.unlink()
    commands=('cd APP\n' if in_app else '')
    commands+=('path C:\\APP\nmicroweb -video=a LOCAL.HTM\necho PATH returned>PATH.TXT' if via_path else 'C:\\RTEXIT C:\\APP\\MICROWEB.EXE')
    if url:
        shutil.copyfile(root/'net/NE2000XT.COM', guest/'NE2000.COM')
        (guest/'MTCP.CFG').write_text('packetint 0x60\nIPADDR 10.0.2.15\nNETMASK 255.255.255.0\nGATEWAY 10.0.2.2\nNAMESERVER 10.0.2.3\n')
        commands='NE2000 0x60 3 0x300\nSET MTCPCFG=C:\\MTCP.CFG\nC:\\RTEXIT C:\\APP\\MICROWEB.EXE '+url
    config=case/'dosbox.conf'
    config.write_text(f'''[sdl]
output=surface
fullscreen=false
mouse_wheel_key=0
[dosbox]
machine=cga
memsize=0
memsizekb=640
splash logo=false
[render]
scaler=none
aspect=false
[cpu]
core=normal
cputype=8086
cycles=fixed 3000
[mixer]
nosound=true
[midi]
mididevice=none
[joystick]
joysticktype=none
[dos]
xms=false
ems=false
umb=false
[ne2000]
ne2000={"true" if url else "false"}
nicbase=300
nicirq=3
backend=slirp
[autoexec]
mount c {guest}
c:
{commands}
exit
''')
    env=dict(os.environ, LD_PRELOAD=str(work/'events.so'), MW_CAPTURE_DIR=str(case), MW_TEST_DELAY='20' if url else '6',
             SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    with (case/'emulator.log').open('wb') as log:
        process=subprocess.Popen([os.getenv('DOSBOX_X','dosbox-x'),'-defaultconf','-conf',str(config)],
                                  stdout=log,stderr=subprocess.STDOUT,env=env,start_new_session=True)
        try: process.wait(timeout=40)
        finally:
            if process.poll() is None: os.killpg(process.pid,signal.SIGKILL); process.wait()
    assert process.returncode==0, label+' did not exit cleanly'
    return result.read_text().strip() if result.exists() else '', (case/'emulator.log').read_text()
new=root/'build/dos/MicroWeb.exe'
if not os.getenv('NETWORK_ONLY'):
    result,log=run('new-executable-path',new)
    assert result=='child=0 break=1 ctrlc=1 timer=1', result
    assert 'WHEELTEST stage=3' in log, 'Browser must remain open until Escape, not fail loading assets'
    result,log=run('new-cwd',new,True)
    assert result=='child=0 break=1 ctrlc=1 timer=1', result
    _,log=run('new-shell-path',new,via_path=True)
    assert 'WHEELTEST stage=3' in log and (work/'new-shell-path/guest/PATH.TXT').exists()
    if os.getenv('BASELINE_EXE'):
        baseline=Path(os.environ['BASELINE_EXE']).resolve()
        result,log=run('baseline-executable-path',baseline)
        assert result.startswith('child=1 ') and 'WHEELTEST stage=3' not in log, result
        result,log=run('baseline-cwd',baseline,True)
        assert result=='child=0 break=0 ctrlc=1 timer=1', result
        print('PASS: baseline reproduces DAT failure and corrupts INT 1Bh on offline exit')
    print('PASS: DOS 8086 launch from PATH and another directory; Escape restores INT 1Bh/23h/1Ch')

if os.getenv('NETWORK_TEST'):
    import http.server
    import threading
    import base64
    received=[]
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args): pass
        def do_GET(self):
            received.append(self.path)
            if self.path in ('/start', '/one', '/two'):
                self.send_response(302)
                self.send_header('Location', {'/start':'/one','/one':'/two','/two':'/page'}[self.path])
                data=b''
            elif self.path=='/page':
                self.send_response(200); self.send_header('Content-Type','text/html')
                data=('<html><head><title>Network lifetime regression</title></head><body><h1>Real mTCP socket reuse</h1>'+''.join('<img src="/image%d.gif">' % n for n in range(8))+'</body></html>').encode()
            else:
                self.send_response(200); self.send_header('Content-Type','image/gif')
                data=base64.b64decode('R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7')
            self.send_header('Content-Length',str(len(data))); self.end_headers(); self.wfile.write(data)
    with http.server.HTTPServer(('127.0.0.1',0),Handler) as server:
        thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
        try:
            result,log=run('new-network',new,url='http://10.0.2.2:%d/start' % server.server_port)
        finally:
            server.shutdown(); thread.join()
    assert result=='child=0 break=1 ctrlc=1 timer=1', result
    assert received[:4]==['/start','/one','/two','/page'], received
    assert set(received[4:])=={'/image%d.gif' % n for n in range(8)}, received
    print('PASS: actual mTCP/NE2000 redirects + eight image requests, followed by clean timer/IRQ restoration')
