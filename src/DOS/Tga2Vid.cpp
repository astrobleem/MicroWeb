// Tandy Video II: linear 640x200 RGBI, not BIOS 0Ah (640x200x4).
// Hardware facts: Joseph A. Albrecht's TANDY11 Mode11/ResetVid tables,
// https://github.com/jasa1063/GRAFIX/blob/main/2.70/SOURCE/TANDY11.ASM
// Identification: Tandy 1000 SL Technical Reference, BIOS pp. 27-28:
// https://www.manualslib.com/manual/1219946/Tandy-1000-Sl.html?page=561
// This is an independent implementation; no resident BIOS extension needed.
#include "Tga2Vid.h"
#include <dos.h>
#include <conio.h>
#include <stdio.h>
#include <string.h>

namespace
{
    const unsigned VideoParagraphs = 0x1000; // All 64K, including unused tail.

    uint8_t ReadByte(unsigned segment, unsigned offset)
    {
        return *(const volatile uint8_t far*)MK_FP(segment, offset);
    }

    uint16_t ReadWord(unsigned segment, unsigned offset)
    {
        return *(const volatile uint16_t far*)MK_FP(segment, offset);
    }

    void WriteByte(unsigned segment, unsigned offset, uint8_t value)
    {
        *(volatile uint8_t far*)MK_FP(segment, offset) = value;
    }

    void WriteWord(unsigned segment, unsigned offset, uint16_t value)
    {
        *(volatile uint16_t far*)MK_FP(segment, offset) = value;
    }

    void ArrayRegister(unsigned index, unsigned value)
    {
        inp(0x3da);
        outp(0x3da, index);
        outp(0x3de, value);
    }

    void CrtcRegister(unsigned index, unsigned value)
    {
        outp(0x3d4, index);
        outp(0x3d5, value);
    }

    uint16_t TagWord(unsigned segment, unsigned word, unsigned salt)
    {
        return (uint16_t)(segment ^ salt ^ (word * 0x1327u));
    }

    void TagAllocation(unsigned segment, unsigned paragraphs, unsigned salt)
    {
        // Only write through the conventional address of our DOS allocation,
        // never through an unverified video aperture.
        for (unsigned p = 0; p < paragraphs; ++p)
            for (unsigned w = 0; w < 8; ++w)
                WriteWord(segment + p, w * 2, TagWord(segment + p, w, salt));
    }

    bool CheckMapping(unsigned allocation, unsigned paragraphs, unsigned salt,
        unsigned& physicalSegment)
    {
        const unsigned base = ReadWord(0xa000, 0) ^ salt;
        const unsigned long end = (unsigned long)allocation + paragraphs;
        if (base < allocation || (unsigned long)base + VideoParagraphs > end)
            return false;
        // Every paragraph must map at the SAME offset inside this allocation.
        // A partially matched aperture or RAM above DOS's arena is not proof.
        for (unsigned p = 0; p < VideoParagraphs; ++p)
            for (unsigned w = 0; w < 8; ++w)
                if (ReadWord(0xa000 + p, w * 2) != TagWord(base + p, w, salt))
                    return false;
        physicalSegment = base;
        return true;
    }

    bool VerifyMapping(unsigned allocation, unsigned paragraphs, unsigned& base)
    {
        unsigned secondBase;
        TagAllocation(allocation, paragraphs, 0x5a3c);
        if (!CheckMapping(allocation, paragraphs, 0x5a3c, base)) return false;
        // A second changed stamp rejects stale contents/coincidental matches.
        TagAllocation(allocation, paragraphs, 0xc3a5);
        return CheckMapping(allocation, paragraphs, 0xc3a5, secondBase) && base == secondBase;
    }
}

bool Tga2Detected()
{
    if (ReadByte(0xffff, 0x000e) != 0xff || ReadByte(0xfc00, 0) != 0x21)
        return false;

    union REGS inregs, outregs;
    struct SREGS segments;
    memset(&inregs, 0, sizeof(inregs));
    inregs.x.ax = 0x1a00; // Display Combination Code: VGA/MCGA, including RLX.
    int86(0x10, &inregs, &outregs);
    if (outregs.h.al == 0x1a) return false;
    memset(&inregs, 0, sizeof(inregs));
    inregs.x.ax = 0x1200;
    inregs.h.bl = 0x10; // Do not program built-in video behind an EGA adapter.
    int86(0x10, &inregs, &outregs);
    if (outregs.h.bl < 4) return false;

    memset(&inregs, 0, sizeof(inregs));
    inregs.x.ax = 0xc000;
    segread(&segments);
    int86x(0x15, &inregs, &outregs, &segments);
    // Tandy's interface signals support through carry; AH is not needed.
    if (outregs.x.cflag) return false;
    const unsigned long address = ((unsigned long)segments.es << 4) + outregs.x.bx;
    if (address < 0xe0000UL || address > 0xffffbUL || outregs.x.bx > 0xfffb)
        return false;
    // Tandy documents a three-byte table payload (model, submodel, revision).
    // The SL reference p562 says all earlier Tandy 1000s return carry set.
    // Keep the family check used by TANDY11; do not invent later submodel IDs.
    // The eight-byte AT-style table synthesized by DOSBox is NOT Video II.
    return ReadWord(segments.es, outregs.x.bx) == 3 &&
        ReadByte(segments.es, outregs.x.bx + 2) == 0xff;
}

Tga2Video::Tga2Video()
    : originalMode(-1), registersChanged(false), allocation(0), framebuffer(0)
{
}

bool Tga2Video::Refuse(const char* reason)
{
    Shutdown();
    printf("TGA-II: %s\n", reason);
    return false;
}

void Tga2Video::SetRegisters()
{
    registersChanged = true;
    outp(0x3d8, 0x13); // Blank while configuring and proving the mapping.
    static const uint8_t timings[] = { 0x71, 0x50, 0x59, 0x0e, 0xff, 6, 0xc8, 0xe2 };
    for (unsigned n = 0; n < sizeof(timings); ++n) CrtcRegister(n, timings[n]);
    CrtcRegister(8, 0);
    CrtcRegister(9, 0); // One scanline per row; 320 bytes/row, no interleave.
    CrtcRegister(0x0c, 0);
    CrtcRegister(0x0d, 0);
    CrtcRegister(0x10, 0x18);
    CrtcRegister(0x11, 0);
    CrtcRegister(0x12, 0x46);
    CrtcRegister(0x13, 0);
    outp(0x3d9, 0);
    outp(0x3dd, 0);
    ArrayRegister(1, 15);
    ArrayRegister(2, 0);
    ArrayRegister(3, 0x10);
    ArrayRegister(5, 1); // Enable the linear A000:0000-FFFF aperture.
    ArrayRegister(8, 2);
    // Lower 64K of the 128K video area for BOTH CPU and display. Unlike the
    // default upper page, this can be wholly owned via an ordinary DOS block.
    outp(0x3df, 0);
    for (unsigned colour = 0; colour < 16; ++colour) ArrayRegister(0x10 + colour, colour);
}

bool Tga2Video::ReserveFramebuffer()
{
    unsigned largest = 0, block = 0;
    if (_dos_allocmem(0xffff, &largest) == 0)
    {
        _dos_freemem(largest); // Defensive: impossible in conventional DOS.
        return false;
    }
    if (largest < VideoParagraphs || _dos_allocmem(largest, &block) != 0)
        return false;
    allocation = (uint16_t)block;
    if (block < 0x0050 || (unsigned long)block + largest > 0xa000UL)
        return false; // UMBs or unexpected allocations are not supported.

    SetRegisters(); // Does not clear or write any framebuffer memory.
    unsigned base;
    if (!VerifyMapping(block, largest, base)) return false;

    unsigned available;
    if (base == block)
    {
        if (_dos_setblock(VideoParagraphs, block, &available)) return false;
        framebuffer = allocation;
        allocation = 0;
    }
    else
    {
        // Let DOS create the new MCB immediately BEFORE the verified 64K.
        // Never edit MCBs or change the BIOS conventional-memory count.
        const unsigned prefix = base - block - 1;
        if (!prefix || _dos_setblock(prefix, block, &available)) return false;
        unsigned reserved;
        if (_dos_allocmem(VideoParagraphs, &reserved)) return false;
        framebuffer = (uint16_t)reserved;
        if (reserved != base) return false; // Different allocation strategy/hole.
        _dos_freemem(allocation);
        allocation = 0;
    }
    // Re-prove the complete aperture after reshaping the DOS blocks. This
    // retained 64K block stays allocated until the hardware leaves the mode.
    unsigned finalBase;
    return VerifyMapping(framebuffer, VideoParagraphs, finalBase) && finalBase == framebuffer;
}

bool Tga2Video::Init()
{
    if (registersChanged || allocation || framebuffer) return false;
    if (!Tga2Detected()) return Refuse("Video II colour hardware not identified (SL/TL/RL, not EX/HX or VGA).");
    union REGS inregs, outregs;
    memset(&inregs, 0, sizeof(inregs));
    inregs.h.ah = 0x0f;
    int86(0x10, &inregs, &outregs);
    if ((outregs.h.al != 2 && outregs.h.al != 3) || ReadWord(0x40, 0x63) != 0x3d4)
        return Refuse("start from 80-column colour DOS text mode.");
    originalMode = outregs.h.al;
    if (!ReserveFramebuffer())
        return Refuse("cannot safely reserve and verify the complete 64K video page.");

    // Tell mouse drivers this is a 640x200 graphics surface, not text (which
    // may quantize the cursor to character cells). MicroWeb draws everything
    // itself and never invokes BIOS pixel/text rendering in this custom mode.
    WriteByte(0x40, 0x49, 6);
    WriteWord(0x40, 0x4a, 80);
    WriteWord(0x40, 0x4c, 64000u);
    WriteWord(0x40, 0x4e, 0);
    WriteByte(0x40, 0x62, 0);
    WriteByte(0x40, 0x65, 0x1b);
    WriteByte(0x40, 0x66, 0);
    // Only now is the mapping trusted. Clear all 64K while blanked.
    for (unsigned p = 0; p < VideoParagraphs; ++p)
        for (unsigned w = 0; w < 8; ++w) WriteWord(0xa000 + p, w * 2, 0);
    outp(0x3d8, 0x1b);
    return true;
}

void Tga2Video::Shutdown()
{
    if (registersChanged)
    {
        // BIOS modes do not reliably reset the Video II extended registers.
        // Disable the aperture and restore these BEFORE asking BIOS for text.
        outp(0x3d8, 1);
        ArrayRegister(5, 0);
        ArrayRegister(8, 0);
        CrtcRegister(0x10, 0x18);
        CrtcRegister(0x11, 0x20);
        CrtcRegister(0x12, 7);
        CrtcRegister(0x13, 0);
        outp(0x3dd, 0);
        outp(0x3df, 0x3f); // Restore the usual top-16K text CPU/display page.
        union REGS inregs, outregs;
        memset(&inregs, 0, sizeof(inregs));
        inregs.x.ax = originalMode;
        int86(0x10, &inregs, &outregs);
        registersChanged = false;
    }
    if (framebuffer) { _dos_freemem(framebuffer); framebuffer = 0; }
    if (allocation) { _dos_freemem(allocation); allocation = 0; }
    originalMode = -1;
}
