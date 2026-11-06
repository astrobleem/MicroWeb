#!/usr/bin/env python3
"""Real DOS/mTCP failed-init allocation test; run tools/build-dos.sh first.
Requires WATCOM, DOSBOX_X. Optional BASELINE_OBJECTS points to old build/dos.
"""
from pathlib import Path
import os
import re
import subprocess
root=Path(__file__).resolve().parent.parent
work=root/'build/packet-init-dos-test'; work.mkdir(parents=True,exist_ok=True)
watcom=Path(os.environ['WATCOM']).resolve()
bin_dir=watcom/('binl64' if (watcom/'binl64/wpp').exists() else 'binl')
env=dict(os.environ,WATCOM=str(watcom),PATH=str(bin_dir)+os.pathsep+os.environ['PATH'],
         INCLUDE=str(watcom/'h'),LIBDOS=str(watcom/'lib286/dos')+os.pathsep+str(watcom/'lib286'),
         SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
subprocess.run([str(bin_dir/'wpp'),'-zq','-0','-ml','-s','-bt=dos','-DCFG_H="TCP.CFG"',
               '-i='+str(root/'build/dos/include'),'-i='+str(root/'project/DOS'),'-fo=stack.obj',
               str(root/'tests/runtime_stack.cpp')],cwd=work,env=env,check=True)
def run(label,objects):
    guest=work/label; guest.mkdir(exist_ok=True)
    objects=Path(objects).resolve()
    files=[str(work/'stack.obj')]+[str(objects/(name+'.obj')) for name in
        ('packet','arp','eth','ip','tcp','tcpsockm','udp','utils','dns','timer','trace','ipasm')]
    subprocess.run([str(bin_dir/'wlink'),'system','dos','name',str(guest/'STACK.EXE'),
        'file',','.join(files),'option','quiet'],cwd=work,env=env,check=True)
    (guest/'MTCP.CFG').write_text('packetint 0x60\nIPADDR 10.0.2.15\nNETMASK 255.255.255.0\nGATEWAY 10.0.2.2\nNAMESERVER 10.0.2.3\n')
    result=guest/'STACK.TXT'
    if result.exists(): result.unlink()
    config=guest/'dosbox.conf'
    config.write_text(f'''[sdl]
output=surface
[dosbox]
machine=cga
memsize=0
memsizekb=640
splash logo=false
[cpu]
core=normal
cputype=8086
cycles=fixed 3000
[mixer]
nosound=true
[midi]
mididevice=none
[ne2000]
ne2000=false
[autoexec]
mount c {guest}
c:
set MTCPCFG=C:\\MTCP.CFG
stack
exit
''')
    with (guest/'emulator.log').open('wb') as log:
        subprocess.run([os.getenv('DOSBOX_X','dosbox-x'),'-defaultconf','-conf',str(config)],
            env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=25)
    text=result.read_text().strip()
    values=dict((k,int(v)) for k,v in re.findall(r'(\w+)=(\d+)',text))
    assert values['failures']==3 and all(values[k]==1 for k in ('break','ctrlc','timer')),text
    return values['before']-values['after'],text
retained,text=run('current',root/'build/dos'); assert retained==0,text
print('PASS: DOS 8086 real mTCP failed initialization retains zero additional DOS memory; vectors unchanged')
if os.getenv('BASELINE_OBJECTS'):
    retained,text=run('baseline',os.environ['BASELINE_OBJECTS'])
    # Some retained packets fit inside existing heap capacity; DOS growth
    # still exceeds two full packet pools across these three failed starts.
    assert retained>2*20*1514,text
    print('PASS: baseline reproduces retained packet buffers (%d bytes across three failures)' % retained)
