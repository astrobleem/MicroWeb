// Tandy 1000 mode 09h shares 98000h-9FFFFh with conventional RAM.
// The usual BIOS 624K limit excludes only its upper half. VRESERVE.COM
// must reserve the lower half BEFORE the C runtime and MicroWeb load.
#include "TndyMem.h"
#include <dos.h>
#include <stdio.h>
#include <stdint.h>

namespace
{
    const uint16_t VideoSegment = 0x9800;
    const uint16_t ArenaLimit = 0x9c00;
    const uint16_t ReservationMCB = 0x97ff;
    const uint16_t ReservationParagraphs = 0x0400;
    const uint16_t ResidentParagraphs = 111;

    uint8_t ReadByte(uint16_t segment, uint16_t offset)
    {
        return *(const volatile uint8_t far*)MK_FP(segment, offset);
    }

    uint16_t ReadWord(uint16_t segment, uint16_t offset)
    {
        return *(const volatile uint16_t far*)MK_FP(segment, offset);
    }

    bool Refuse(const char* reason)
    {
        printf("Tandy video memory: %s\n", reason);
        printf("Use TANDY.BAT from DOS text mode before starting MicroWeb.\n");
        return false;
    }

    // Called only after the complete chain has been validated. An owned
    // 16K block alone is NOT evidence of a reservation: it could be our heap.
    bool HasResidentOwner(uint16_t first, uint16_t currentPSP)
    {
        const uint16_t owner = ReadWord(ReservationMCB, 1);
        if (!owner || owner == currentPSP || owner >= ReservationMCB)
            return false;

        uint16_t mcb = first;
        while (mcb < ReservationMCB)
        {
            const uint16_t paragraphs = ReadWord(mcb, 3);
            if ((uint16_t)(mcb + 1) == owner)
            {
                if (ReadByte(mcb, 0) != 'M' ||
                    ReadWord(mcb, 1) != owner ||
                    paragraphs != ResidentParagraphs ||
                    (unsigned long)owner + paragraphs > VideoSegment)
                    return false;

                // Original RESERVE and VRESERVE share this resident ABI.
                // PSP:0100 is a 3-byte jump; marker begins at PSP:0103.
                static const char marker[] = "TNDYRS01";
                for (uint16_t n = 0; n < 8; ++n)
                    if (ReadByte(owner, 0x0103 + n) != marker[n])
                        return false;
                return ReadWord(owner, 0x010b) == VideoSegment &&
                    ReadWord(owner, 0x010d) == ReservationParagraphs;
            }
            mcb = (uint16_t)(mcb + paragraphs + 1);
        }
        return false;
    }
}

bool TandyVideoMemoryReady()
{
    // This guard covers the original, normal-640K Tandy layout only. Do not
    // infer framebuffer isolation merely from a larger BIOS memory count.
    if (ReadByte(0xffff, 0x000e) != 0xff ||
        ReadByte(0xfc00, 0x0000) != 0x21)
        return Refuse("original Tandy 1000 ROM signature not found.");
    if (ReadWord(0x0040, 0x0013) != 624)
        return Refuse("unsupported BIOS memory layout (624K required).");

    union REGS inregs, outregs;
    struct SREGS segments;
    inregs.h.ah = 0x30;
    int86(0x21, &inregs, &outregs);
    if (outregs.h.al < 3)
        return Refuse("DOS 3 or newer is required.");

    inregs.h.ah = 0x51;
    int86(0x21, &inregs, &outregs);
    const uint16_t currentPSP = outregs.x.bx;

    segread(&segments);
    inregs.h.ah = 0x52;
    int86x(0x21, &inregs, &outregs, &segments);
    if (outregs.x.bx < 2)
        return Refuse("DOS arena information is unavailable.");
    const uint16_t first = ReadWord(segments.es, outregs.x.bx - 2);
    uint16_t mcb = first;
    if (mcb < 0x0050)
        return Refuse("invalid DOS arena start.");

    // Positive progress and a fixed upper bound make this walk finite even
    // on a corrupt chain. Linked UMBs and unexpected conventional layouts
    // are deliberately rejected. Never modify a DOS MCB or the BIOS size.
    for (;;)
    {
        if (mcb >= ArenaLimit)
            return Refuse("unsupported DOS arena boundary.");
        const uint8_t type = ReadByte(mcb, 0);
        if (type != 'M' && type != 'Z')
            return Refuse("invalid DOS memory-control block.");
        const uint16_t paragraphs = ReadWord(mcb, 3);
        const unsigned long next = (unsigned long)mcb + paragraphs + 1;
        if (next > ArenaLimit || next <= mcb)
            return Refuse("unsupported or corrupt DOS arena.");
        if (type == 'Z')
        {
            // A genuinely shortened DOS arena needs no high allocation.
            // Changing only the BIOS memory-size word would not prove this.
            if (next <= VideoSegment)
                return true;
            if (next == ArenaLimit && mcb == ReservationMCB &&
                paragraphs == ReservationParagraphs &&
                HasResidentOwner(first, currentPSP))
                return true;
            return Refuse("video overlap is not safely reserved.");
        }
        mcb = (uint16_t)next;
    }
}
