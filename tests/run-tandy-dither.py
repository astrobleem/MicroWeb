#!/usr/bin/env python3
"""Exercise the production RGBI emitters, including unchanged alpha coverage."""
from pathlib import Path
import random, re, subprocess, tempfile
from PIL import Image
root=Path(__file__).resolve().parents[1]
text=(root/'src/TandyDither.inc').read_text()
pairs=[int(n,16) for n in re.findall(r'0x([0-9a-f]+)',text.split('tandyYCCDitherPairs')[0])]
assert len(pairs)==4096
order=(0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5)
rng=random.Random(7103); checks=0

def decode(kind,path,fragment,out=(0,0),bpp=8,tandy=True):
    global checks
    command=[str(root/f'build/tests/{kind}-decode'),str(path),str(fragment),str(bpp),'0',str(out[0]),str(out[1])]
    if kind=='png':command.append('-1')
    command.append('tandy' if tandy else 'generic')
    p=subprocess.run(command,capture_output=True,check=True)
    head,pixels=p.stdout.split(b'\n',1)
    state,w,h,lines,size=map(int,head.split());assert state==2,(kind,head,p.stderr)
    assert lines==h,(kind,head)
    if bpp==8:assert len(pixels)==w*h
    checks+=1
    return pixels

def expected(image,out):
    w,h=image.size;ow,oh=out if out[0] else (w,h);source=image.convert('RGBA');result=bytearray()
    for y in range(oh):
        sy=((y+1)*h+oh-1)//oh-1
        for x in range(ow):
            r,g,b,a=source.getpixel((x*w//ow,sy));phase=order[(y&3)*4+(x&3)]
            if not a or (a<255 and a<=phase*16+8):result.append(255);continue
            if a<255:phase=int(f'{phase:04b}'[::-1],2)
            pair=pairs[(r>>4)*256+(g>>4)*16+(b>>4)]
            result.append((pair>>4)&15 if phase<pair>>8 else pair&15)
    return bytes(result)

with tempfile.TemporaryDirectory(prefix='tandy-dither-') as td:
    p=Path(td)/'image.png'
    # RGBA, grayscale+alpha, indexed alpha and RGB colour-key transparency.
    rgba=Image.new('RGBA',(23,19));rgba.putdata([(rng.randrange(256),rng.randrange(256),rng.randrange(256),(0,1,8,9,64,128,192,247,254,255)[n%10]) for n in range(23*19)])
    images=[rgba,rgba.convert('LA'),rgba.convert('RGB').quantize(colors=32)]
    images[2].info['transparency']=bytes((0,64,128,255)[n%4] for n in range(32))
    key=rgba.convert('RGB');key.info['transparency']=key.getpixel((0,0));images.append(key)
    for image in images:
        image.save(p)
        # Reopen so the independent reference also observes PNG palette/tRNS.
        loaded=Image.open(p);loaded.load()
        for out in ((0,0),(11,7),(31,29)):
            want=expected(loaded,out)
            for fragment in (1,2,7,63,4096):assert decode('png',p,fragment,out)==want
            mono=decode('png',p,1,out,1)
            assert mono==decode('png',p,7,out,1,tandy=False)
    # Alpha/colour rank independence: half-covered gray must still contain
    # both dark and light mixture members, instead of choosing one endpoint.
    half=Image.new('RGBA',(16,16),(128,128,128,128));half.save(p)
    actual=decode('png',p,1);visible=[n for n in actual if n!=255]
    assert len(visible)==128 and len(set(visible))==2
    for width,height in ((1,1),(17,13),(37,21)):
        im=Image.new('RGB',(width,height));im.putdata([(rng.randrange(256),rng.randrange(256),rng.randrange(256)) for n in range(width*height)])
        for mode in ('L','RGB'):
            for subsampling in ((0,) if mode=='L' else (0,1,2)):
                p=Path(td)/'image.jpg';im.convert(mode).save(p,quality=85,subsampling=subsampling)
                for out in ((0,0),(19,17),(7,5)):
                    golden=decode('jpeg',p,4096,out)
                    assert all(n<16 for n in golden)
                    if mode=='L':assert set(golden)<={0,7,8,15}
                    for fragment in (1,2,7,63):assert decode('jpeg',p,fragment,out)==golden
                    assert decode('jpeg',p,1,out,1)==decode('jpeg',p,7,out,1,tandy=False)
print(f'PASS: {checks} production Tandy PNG/JPEG decodes; exact alpha mask and colour reference, partial-alpha colour balance, all JPEG subsamplings, odd sizes, resize, fragment invariance and monochrome isolation')
