#!/usr/bin/env python3
"""Optional Linux/SDL2 DOSBox-X end-to-end test of the built DOS executable.
Set DOSBOX_X to the emulator executable or a headless wrapper. Set BASELINE_EXE
optionally to verify that a pre-wheel binary ignores the exact same events.
Requires Pillow, a C compiler, and tools/build-dos.sh outputs in build/dos.
"""
from pathlib import Path
from PIL import Image
import os
import shutil
import signal
import subprocess

root = Path(__file__).resolve().parent.parent
work = root / 'build/wheel-dos-test'
work.mkdir(parents=True, exist_ok=True)
guest = work / 'guest'
guest.mkdir(exist_ok=True)
for name in ('MicroWeb.exe', 'VRESERVE.COM'):
    shutil.copyfile(root / 'build/dos' / name, guest / name)
for name in ('Default.dat', 'LowRes.dat', 'CGA.dat', 'EGA.dat'):
    shutil.copyfile(root / name, guest / name)
(guest / 'SCROLL.HTM').write_text(
    '<html><head><title>Mouse wheel scrolling</title></head><body>' +
    ''.join(f'<p>Line {n:02d}: Wheel test row number {n:02d}.</p>\n' for n in range(1, 31)) +
    '</body></html>')
subprocess.run([os.getenv('CC', 'cc'), '-shared', '-fPIC',
                str(root / 'tests/wheel_sdl.c'), '-ldl', '-o', str(work / 'wheel-sdl.so')], check=True)

def run(label, executable):
    destination = guest / f'{label}.EXE'
    shutil.copyfile(executable, destination)
    captures = work / label
    captures.mkdir(exist_ok=True)
    for old in captures.glob('*.bmp'):
        old.unlink()
    config = work / f'{label}.conf'
    config.write_text(f'''[sdl]
output=surface
fullscreen=false
mouse_wheel_key=0
[dosbox]
machine=tandy
splash logo=false
show basic information=false
memsize=0
memsizekb=640
allow more than 640kb base memory=false
[render]
aspect=false
scaler=none
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
[autoexec]
mount c {guest}
c:
vreserve.com
{label} -video=t C:/SCROLL.HTM
exit
''')
    env = dict(os.environ, MW_CAPTURE_DIR=str(captures), LD_PRELOAD=str(work / 'wheel-sdl.so'),
               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    with (work / f'{label}.log').open('wb') as log:
        process = subprocess.Popen([os.getenv('DOSBOX_X', 'dosbox-x'), '-defaultconf', '-conf', str(config)],
                                   env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            process.wait(timeout=20)
            assert process.returncode == 0, f'{label} emulator failed; see {log.name}'
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
    assert 'WHEELTEST stage=3' in (work / f'{label}.log').read_text(), 'All input stages must run'
    def frame_before(deadline):
        eligible = [p for p in captures.glob('*.bmp') if float(p.stem.split('-')[-1]) < deadline]
        assert eligible, 'No SDL frames captured'
        path = max(eligible, key=lambda p: float(p.stem.split('-')[-1]))
        return Image.open(path).convert('RGB')
    return [frame_before(t) for t in (6, 8, 10, 12)]

initial, down_two, up_one, restored = run('NEW', root / 'build/dos/MicroWeb.exe')
assert initial.tobytes() != down_two.tobytes(), 'Two downward notches must scroll'
assert down_two.tobytes() != up_one.tobytes(), 'One upward notch must scroll back'
assert initial.tobytes() == restored.tobytes(), 'Second upward notch must restore exact initial frame'
# DOSBox-X's unscaled 320x200 framebuffer is centered in its 500x317 window.
# Exclude stationary cursor, toolbar and scrollbar; page pixels must move by 48/24.
assert initial.crop((90, 145, 240, 250)).tobytes() == down_two.crop((90, 97, 240, 202)).tobytes()
assert initial.crop((90, 121, 240, 250)).tobytes() == up_one.crop((90, 97, 240, 226)).tobytes()
if os.getenv('BASELINE_EXE'):
    old = run('OLD', Path(os.environ['BASELINE_EXE']).resolve())
    assert all(image.tobytes() == old[0].tobytes() for image in old), 'Baseline must ignore wheel events'
    assert old[0].tobytes() == initial.tobytes(), 'Baseline and new initial rendering must match'
print('PASS: real DOS WheelAPI through SDL2/DOSBox-X; down 48px, up 24px, exact return, normal exit')
