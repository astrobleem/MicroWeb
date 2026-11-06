#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <dos.h>
#include "../src/DOS/Tga2Vid.h"

static unsigned char ram[1024 * 1024], stale[65536];
static unsigned char arrays[32], crtc[32];
static unsigned arrayIndex, crtcIndex, page, mode, portWrites, restores, cases;
static bool vga, ega, tableCarry, unchangedAH, noMemory, failResize, wrongAllocation;
static bool foldMapping, staleMapping, failAfterResize, resized, snapshot;
static int badParagraph;
static unsigned physicalBase, initialBase, initialSize, tableSegment, tableOffset;
static unsigned apertureReads[4096];
struct Block { unsigned base, size; bool owned; };
static std::vector<Block> blocks;

static void word(unsigned at, unsigned value) { ram[at] = value & 255; ram[at + 1] = value >> 8; }
static unsigned readWord(unsigned at) { return ram[at] | (unsigned)ram[at + 1] << 8; }

void* Tga2TestAddress(unsigned segment, unsigned offset)
{
    unsigned long at = (unsigned long)segment * 16 + offset;
    assert(at < sizeof(ram));
    if (at >= 0xa0000 && at < 0xb0000 && (arrays[5] & 1))
    {
        unsigned off = at - 0xa0000, p = off >> 4;
        ++apertureReads[p];
        if ((int)p == badParagraph || (failAfterResize && resized)) return ram + at;
        if (staleMapping)
        {
            if (!snapshot) { memcpy(stale, ram + physicalBase * 16, sizeof(stale)); snapshot = true; }
            return stale + off;
        }
        if (foldMapping) off &= 32767;
        return ram + physicalBase * 16 + off;
    }
    return ram + at;
}
void segread(SREGS* s) { s->es = 0; }
void int86(int interrupt, REGS* in, REGS* out)
{
    assert(interrupt == 0x10);
    *out = *in;
    if (in->x.ax == 0x1a00) { out->h.al = vga ? 0x1a : 0; return; }
    if (in->x.ax == 0x1200) { out->h.bl = ega ? 0 : 0x10; return; }
    if (in->h.ah == 0x0f) { out->h.al = mode; return; }
    if (!in->h.ah)
    {
        assert(in->h.al == 2 || in->h.al == 3);
        assert(arrays[5] == 0 && arrays[8] == 0 && page == 0x3f);
        assert(crtc[0x10] == 0x18 && crtc[0x11] == 0x20 && crtc[0x12] == 7 && crtc[0x13] == 0);
        ++restores;
        mode = in->h.al;
        ram[0x449] = mode;
        return;
    }
    assert(!"Unexpected BIOS call");
}
void int86x(int interrupt, REGS* in, REGS* out, SREGS* s)
{
    assert(interrupt == 0x15 && in->x.ax == 0xc000);
    memset(out, 0, sizeof(*out));
    out->x.cflag = tableCarry;
    out->h.ah = unchangedAH ? 0xc0 : 0;
    s->es = tableSegment;
    out->x.bx = tableOffset;
}
int inp(unsigned port) { assert(port == 0x3da); return 0; }
int outp(unsigned port, unsigned value)
{
    assert(value <= 255); ++portWrites;
    if (port == 0x3da) { assert(value < 32); arrayIndex = value; }
    else if (port == 0x3de) arrays[arrayIndex] = value;
    else if (port == 0x3d4) { assert(value < 32); crtcIndex = value; }
    else if (port == 0x3d5) crtc[crtcIndex] = value;
    else if (port == 0x3df) { page = value; assert(page == (arrays[5] ? 0u : 0x3fu)); }
    else assert(port == 0x3d8 || port == 0x3d9 || port == 0x3dd);
    return value;
}
unsigned _dos_allocmem(unsigned n, unsigned* segment)
{
    if (noMemory) { *segment = 0; return 8; }
    unsigned largest = 0;
    for (unsigned i = 0; i < blocks.size(); ++i)
    {
        Block b = blocks[i];
        if (!b.owned && b.size > largest) largest = b.size;
        if (!b.owned && b.size >= n)
        {
            if (wrongAllocation && resized) { *segment = 0x4000; blocks.push_back(Block{0x4000, n, true}); return 0; }
            *segment = b.base;
            blocks[i].owned = true;
            blocks[i].size = n;
            if (b.size > n) blocks.push_back(Block{b.base + n + 1, b.size - n - 1, false});
            return 0;
        }
    }
    *segment = largest;
    return 8;
}
unsigned _dos_setblock(unsigned n, unsigned segment, unsigned* available)
{
    if (failResize) { *available = 0; return 8; }
    for (unsigned i = 0; i < blocks.size(); ++i)
    {
        Block b = blocks[i];
        if (b.base == segment && b.owned)
        {
            assert(n <= b.size);
            blocks[i].size = n;
            if (n < b.size) blocks.push_back(Block{segment + n + 1, b.size - n - 1, false});
            resized = true;
            return 0;
        }
    }
    assert(!"Resize of unowned memory"); return 9;
}
unsigned _dos_freemem(unsigned segment)
{
    for (unsigned i = 0; i < blocks.size(); ++i)
        if (blocks[i].base == segment && blocks[i].owned)
        {
            // The live framebuffer may only be freed AFTER aperture shutdown.
            if (segment == physicalBase && blocks[i].size == 4096) assert(!arrays[5]);
            blocks[i].owned = false; return 0;
        }
    assert(!"Free of unowned memory"); return 9;
}
static void reset(unsigned base = 0x5000, unsigned size = 0x4c00)
{
    memset(ram, 0xa5, sizeof(ram)); memset(stale, 0, sizeof(stale));
    memset(arrays, 0, sizeof(arrays)); memset(crtc, 0, sizeof(crtc));
    memset(apertureReads, 0, sizeof(apertureReads));
    arrayIndex = crtcIndex = page = portWrites = restores = 0;
    vga = ega = tableCarry = unchangedAH = noMemory = failResize = wrongAllocation = false;
    foldMapping = staleMapping = failAfterResize = resized = snapshot = false;
    badParagraph = -1; mode = 3; physicalBase = 0x8000;
    initialBase = base; initialSize = size;
    tableSegment = 0xf000; tableOffset = 0x100;
    ram[0xffffe] = 0xff; ram[0xfc000] = 0x21;
    word(0xf0100, 3); ram[0xf0102] = 0xff; ram[0xf0103] = 0; ram[0xf0104] = 0;
    word(0x463, 0x3d4); ram[0x449] = 3;
    blocks.clear(); blocks.push_back(Block{base, size, false});
}
static void unchangedAperture()
{
    for (unsigned i = 0xa0000; i < 0xb0000; ++i) assert(ram[i] == 0xa5);
}
static void expectRefusal(bool touched = false)
{
    Tga2Video video;
    assert(!video.Init());
    assert((portWrites != 0) == touched);
    assert(restores == (touched ? 1u : 0u));
    for (unsigned i = 0; i < blocks.size(); ++i) assert(!blocks[i].owned);
    assert(!arrays[5] && !arrays[8]);
    unchangedAperture();
    video.Shutdown();
    assert(restores == (touched ? 1u : 0u));
    ++cases;
}
static void expectSuccess()
{
    Tga2Video video; assert(video.Init());
    unsigned owned = 0;
    for (unsigned i = 0; i < blocks.size(); ++i) if (blocks[i].owned)
    { ++owned; assert(blocks[i].base == physicalBase && blocks[i].size == 4096); }
    assert(owned == 1);
    assert(arrays[5] == 1 && arrays[8] == 2 && arrays[3] == 0x10);
    const unsigned char timings[] = {0x71,0x50,0x59,0x0e,0xff,6,0xc8,0xe2};
    assert(memcmp(crtc, timings, 8) == 0);
    assert(crtc[8] == 0 && crtc[9] == 0 && crtc[0x0c] == 0 && crtc[0x0d] == 0);
    for (unsigned i = 0; i < 16; ++i) assert(arrays[0x10 + i] == i);
    for (unsigned i = 0; i < 4096; ++i) assert(apertureReads[i] >= 40);
    for (unsigned i = 0; i < 65536; ++i) assert(ram[physicalBase * 16 + i] == 0);
    assert(ram[0x449] == 6 && readWord(0x44a) == 80 && readWord(0x44c) == 64000);
    // Writes via the trusted aperture reach the retained DOS block.
    *(unsigned char*)Tga2TestAddress(0xa000, 63999) = 0x12;
    assert(ram[physicalBase * 16 + 63999] == 0x12);
    unchangedAperture();
    video.Shutdown(); video.Shutdown();
    assert(restores == 1 && mode == 3);
    for (unsigned i = 0; i < blocks.size(); ++i) assert(!blocks[i].owned);
    ++cases;
}
int main()
{
    reset(); assert(Tga2Detected()); assert(!portWrites); expectSuccess();
    reset(0x8000, 0x1c00); expectSuccess(); // No prefix allocation required.
    reset(0x3000, 0x4c00); physicalBase = 0x6000; expectSuccess(); // Different installed RAM.
    reset(); ram[0xffffe] = 0xfc; expectRefusal();
    reset(); ram[0xfc000] = 0; expectRefusal();
    reset(); vga = true; expectRefusal();
    reset(); ega = true; expectRefusal();
    reset(); tableCarry = true; expectRefusal();
    reset(); unchangedAH = true; expectSuccess();
    reset(); ram[0xf0102] = 0xfc; expectRefusal();
    for (unsigned length = 0; length <= 4; ++length) if (length != 3)
    { reset(); word(0xf0100, length); expectRefusal(); }
    reset(); word(0xf0100, 8); ram[0xf0103] = 10; ram[0xf0104] = 16; expectRefusal(); // DOSBox Tandy.
    reset(); tableSegment = 0x8000; expectRefusal();
    reset(); tableSegment = 0xffff; tableOffset = 0xfffc; expectRefusal();
    reset(); mode = 9; expectRefusal();
    reset(); word(0x463, 0x3b4); expectRefusal();
    reset(); noMemory = true; expectRefusal();
    reset(0x8000, 0xfff); expectRefusal();
    reset(0xa000, 0x1000); expectRefusal(); // UMB allocation.
    reset(); physicalBase = 0xb000; expectRefusal(true); // Dedicated/unknown video RAM.
    reset(); foldMapping = true; expectRefusal(true); // Only32K actually mapped.
    reset(); staleMapping = true; expectRefusal(true); // First tags match; second do not.
    reset(); failResize = true; expectRefusal(true);
    reset(); wrongAllocation = true; expectRefusal(true);
    reset(); failAfterResize = true; expectRefusal(true);
    const int faults[] = {0, 1, 511, 1023, 2047, 2048, 3071, 4095};
    for (unsigned i = 0; i < sizeof(faults)/sizeof(faults[0]); ++i)
    { reset(); badParagraph = faults[i]; expectRefusal(true); }
    printf("PASS: %u production TGA-II identification, complete64K ownership, register, cleanup and refusal cases\n", cases);
}
