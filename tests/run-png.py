#!/usr/bin/env python3
"""Compare the production streaming PNG decoder against generated exact pixels."""
import os, pathlib, random, struct, subprocess, sys, tempfile, zlib
root = pathlib.Path(__file__).resolve().parents[1]
runner = root / 'build/tests/png-decode'
rng = random.Random(717)
checks = 0
signature = b'\x89PNG\r\n\x1a\n'
order = [0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5]

def chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))

def png(w, h, depth, kind, rows, filters=(0,), palette=None, trans=None, compress='dynamic', split=0, extra=b'', interlace=0):
    bpp = max(1, (({0:1,2:3,3:1,4:2,6:4}[kind]) * depth + 7) // 8)
    raw, prev = bytearray(), bytes(len(rows[0]))
    for y, row in enumerate(rows):
        f = filters[y % len(filters)]; raw.append(f)
        for x, val in enumerate(row):
            a = row[x-bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x-bpp] if x >= bpp else 0
            p = a+b-c
            pa,pb,pc = abs(p-a),abs(p-b),abs(p-c)
            predictor = [0,a,b,(a+b)//2,a if pa <= pb and pa <= pc else b if pb <= pc else c][min(f,4)]
            raw.append((val-predictor)&255)
        prev = row
    co = zlib.compressobj(0 if compress == 'stored' else 9, zlib.DEFLATED, 15, 8, zlib.Z_FIXED if compress == 'fixed' else zlib.Z_DEFAULT_STRATEGY)
    data = co.compress(raw)+co.flush()
    result = signature + chunk(b'IHDR', struct.pack('>IIBBBBB',w,h,depth,kind,0,0,interlace))
    if palette is not None: result += chunk(b'PLTE',bytes(sum((list(x) for x in palette),[])))
    if trans is not None: result += chunk(b'tRNS', trans)
    result += extra
    if split:
        for x in range(0,len(data),split): result += chunk(b'IDAT', data[x:x+split])
    else: result += chunk(b'IDAT', data)
    return result + chunk(b'IEND',b'')

def expected(pixels, w, h, out=None, mono=False):
    ow,oh = out or (w,h)
    result=bytearray()
    for y in range(oh):
        # EmitRow uses the last input row covering this output boundary.
        sy = ((y+1)*h+oh-1)//oh-1
        packed = 0
        for x in range(ow):
            r,g,b,a = pixels[sy][x*w//ow]
            if mono:
                # Use the same standard 16x16 Bayer construction as Decoder.cpp.
                v=0
                xx,yy=x&15,y&15
                for bit in range(4):
                    v |= (((xx>>bit)&1)^((yy>>bit)&1)) << (7-bit*2)
                    v |= ((yy>>bit)&1) << (6-bit*2)
                grey=(r*76+g*150+b*30)>>8
                grey=(grey*a+255*(255-a)+127)//255
                if grey>v: packed |= 128>>(x&7)
                if (x&7)==7 or x==ow-1: result.append(packed); packed=0
            else:
                result.append(255 if not a or (a<255 and a<=order[(y&3)*4+(x&3)]*16+8) else (r&224)|((g&224)>>3)|((b&192)>>6))
    return bytes(result)

with tempfile.TemporaryDirectory(prefix='microweb-png-') as td:
    path=pathlib.Path(td)/'case.png'
    def check(data, want=None, state=2, fragments=(1,2,7,63,4096), out=None, mono=False, dimensions=False, allocation=-1):
        global checks
        path.write_bytes(data)
        for fragment in fragments:
            cmd=[str(runner),str(path),str(fragment),'1' if mono else '8','1' if dimensions else '0',str(out[0] if out else 0),str(out[1] if out else 0),str(allocation)]
            p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            assert p.returncode == 0, p.stderr.decode(errors='replace')
            header, actual=p.stdout.split(b'\n',1)
            st,w,h,lines,workspace=map(int,header.split())
            assert st == state, (checks,fragment,header,state)
            assert workspace < 43000, workspace
            if state != 2: assert lines == 0, (header, "unchecked PNG rows were published")
            if state==2 and want is not None: assert actual==want, (checks, fragment,header,actual[:100],want[:100])
            checks += 1
    # Every colour type, every filter, all legal packed depths, alpha/tRNS,
    # arbitrary IDAT/network boundaries and all three DEFLATE block types.
    for kind in (0,2,3,4,6):
        for depth in ((1,2,4,8) if kind in (0,3) else (8,)):
            w,h=19,15; palette=[(n*17,(15-n)*17,(n^5)*17) for n in range(min(1<<depth,16))] if kind==3 else None
            rows=[]; pixels=[]
            for y in range(h):
                row=bytearray(); pix=[]; accum=bits=0
                for x in range(w):
                    channels={0:1,2:3,3:1,4:2,6:4}[kind]
                    values=[rng.randrange(1<<depth) for _ in range(channels)]
                    if kind==3: values[0]%=len(palette)
                    sample=values[0]
                    if kind==3: rgb=palette[sample]; a=(0,64,128,255)[sample%4]
                    elif kind in (0,4): rgb=(sample*255//((1<<depth)-1),)*3; a=values[1] if kind==4 else (0 if sample==0 else 255)
                    else: rgb=tuple(values[:3]); a=values[3] if kind==6 else 255
                    pix.append((*rgb,a))
                    if depth<8:
                        accum=(accum<<depth)|sample; bits+=depth
                        if bits==8: row.append(accum); accum=bits=0
                    else: row.extend(values)
                if bits: row.append(accum<<(8-bits))
                rows.append(bytes(row)); pixels.append(pix)
            trans = bytes((0,64,128,255)[n%4] for n in range(len(palette))) if kind==3 else struct.pack('>H',0) if kind==0 else None
            data=png(w,h,depth,kind,rows,(0,1,2,3,4),palette,trans,split=1)
            check(data,expected(pixels,w,h))
            check(data,expected(pixels,w,h,(11,7)),out=(11,7),fragments=(1,4096))
            check(data,expected(pixels,w,h,(31,21)),out=(31,21),fragments=(7,))
            check(data,expected(pixels,w,h,mono=True),mono=True,fragments=(1,))
    w,h=200,100
    rows=[bytes(rng.randrange(16)*17 for _ in range(w*3)) for y in range(50)]
    rows+=rows[:]
    pixels=[[(*row[x:x+3],255) for x in range(0,len(row),3)] for row in rows]
    for compression in ('stored','fixed','dynamic'):
        data=png(w,h,8,2,rows,(0,1,2,3,4),compress=compression,split=113)
        check(data,expected(pixels,w,h),fragments=(1,7,4096))
    # Maximum row size, full window wrap, large ancillary chunk and CRC.
    rows=[bytes(rng.randrange(256) for _ in range(4096)) for y in range(9)]
    pixels=[[tuple(row[x:x+4]) for x in range(0,4096,4)] for row in rows]
    data=png(1024,9,8,6,rows,(4,3,2,1,0),extra=chunk(b'tEXt',b'x'*70000))
    check(data,expected(pixels,1024,9),fragments=(1,4096))
    # RGB colour-key transparency and unsupported image layout-only behavior.
    rows=[bytes((255,0,0,0,255,0,0,0,255))]*5
    pix=[[(255,0,0,255),(0,255,0,0),(0,0,255,255)]]*5
    data=png(3,5,8,2,rows,(0,1,2,3,4),trans=struct.pack('>HHH',0,255,0))
    check(data,expected(pix,3,5))
    check(data, state=3,fragments=(1,),allocation=0)
    check(data, state=3,fragments=(7,),allocation=2)
    for depth,interlace,rows in ((16,0,[bytes(12)]*3),(8,1,[bytes(6)]*3)):
        data=png(2,3,depth,2,rows,interlace=interlace)
        check(data,state=3,fragments=(1,))
        check(data,dimensions=True,fragments=(1,))
    # Stored data spanning multiple 65535-byte blocks, with no full-image buffer.
    rows=[bytes((y*7+x*3)&255 for x in range(300)) for y in range(300)]
    pixels=[[(*row[x:x+3],255) for x in range(0,300,3)] for row in rows]
    check(png(100,300,8,2,rows,compress='stored'),expected(pixels,100,300),fragments=(1,4096))
    # A matrix of independently generated small images catches filter/scaler
    # interactions across varying dimensions, content and compression trees.
    for case in range(30):
        w,h=rng.randrange(1,61),rng.randrange(1,21)
        rows=[bytes(rng.randrange(256) for _ in range(w*4)) for _ in range(h)]
        pixels=[[tuple(row[x:x+4]) for x in range(0,w*4,4)] for row in rows]
        data=png(w,h,8,6,rows,tuple(rng.randrange(5) for _ in range(7)),compress=('stored','fixed','dynamic')[case%3],split=rng.randrange(1,71))
        check(data,expected(pixels,w,h),fragments=(rng.randrange(1,301),))
    good=png(2,2,8,2,[bytes((255,0,0,0,255,0))]*2)
    check(good[:-1],state=1,fragments=(1,))
    bad=bytearray(good);bad[29]^=1;check(bad,state=3,fragments=(1,)) # IHDR CRC
    bad=bytearray(good);bad[-5]^=1;check(bad,state=3,fragments=(1,)) # IEND CRC
    # Invalid filter with valid chunk CRC and zlib checksum.
    raw=zlib.compress(b'\x05'+bytes(6)+b'\0'+bytes(6))
    header=good[:33]
    check(header+chunk(b'IDAT',raw)+chunk(b'IEND',b''),state=3,fragments=(1,))
    # Adler error but valid PNG CRC, absent IDAT, unknown critical chunk,
    # nonconsecutive IDAT, too many samples, missing/short palette.
    raw=bytearray(zlib.compress(b'\0'+bytes(6)+b'\0'+bytes(6)));raw[-1]^=1
    check(header+chunk(b'IDAT',raw)+chunk(b'IEND',b''),state=3,fragments=(1,))
    # These have valid PNG CRCs, so failures exercise DEFLATE validation.
    for malformed in (b'\x78\x01\x07', b'\x78\x01\x01\x01\x00\x00\x00', b'\x78\x01\xfd\x00\x00'):
        check(header+chunk(b'IDAT',malformed)+chunk(b'IEND',b''),state=3,fragments=(1,))
    check(header+chunk(b'IDAT',zlib.compress(b'\0'+bytes(6)))+chunk(b'IEND',b''),state=3,fragments=(1,))
    check(good,state=3,fragments=(1,),out=(1025,2))
    check(good,state=3,fragments=(1,),out=(2,4097))
    check(header+chunk(b'IEND',b''),state=3,fragments=(1,))
    check(header+chunk(b'NOPE',b'')+good[33:],state=3,fragments=(1,))
    compressed=zlib.compress(b'\0'+bytes(6)+b'\0'+bytes(6))
    check(header+chunk(b'IDAT',compressed[:3])+chunk(b'tEXt',b'x')+chunk(b'IDAT',compressed[3:])+chunk(b'IEND',b''),state=3,fragments=(1,))
    check(header+chunk(b'IDAT',zlib.compress(b'\0'+bytes(6)+b'\0'+bytes(6)+b'x'))+chunk(b'IEND',b''),state=3,fragments=(1,))
    check(png(3,1,8,3,[bytes((0,1,2))]),state=3,fragments=(1,))
    check(png(3,1,8,3,[bytes((0,1,2))],palette=[(0,0,0)]),state=3,fragments=(1,))
print(f'PASS: {checks} PNG pixel/state checks; all filters/types/depths, stored/fixed/dynamic DEFLATE, split IDAT, window wrap, scaling, transparency, monochrome, allocation and malformed-input handling')
