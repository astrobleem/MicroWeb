#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "../src/DOS/TndyMem.h"

uint8_t tandyTestRAM[1024 * 1024];
static uint8_t originalRAM[sizeof(tandyTestRAM)];
static uint16_t currentPSP;
static unsigned casesRun;

void segread(SREGS* segments)
{
    segments->es = 0;
}

void int86(int interrupt, REGS* input, REGS* output)
{
    assert(interrupt == 0x21);
    const uint8_t operation = input->h.ah;
    memset(output, 0, sizeof(*output));
    switch (operation)
    {
        case 0x30: output->h.al = 3; break;
        case 0x51: output->x.bx = currentPSP; break;
        default: assert(!"Unexpected DOS call"); abort();
    }
}

void int86x(int interrupt, REGS* input, REGS* output, SREGS* segments)
{
    assert(interrupt == 0x21 && input->h.ah == 0x52);
    memset(output, 0, sizeof(*output));
    segments->es = 0x40;
    output->x.bx = 0x22;
}

static void WriteWord(unsigned segment, unsigned offset, unsigned value)
{
    tandyTestRAM[segment * 16 + offset] = value & 0xff;
    tandyTestRAM[segment * 16 + offset + 1] = value >> 8;
}

static void SetBlock(unsigned segment, char type, unsigned owner, unsigned size)
{
    tandyTestRAM[segment * 16] = type;
    WriteWord(segment, 1, owner);
    WriteWord(segment, 3, size);
}

static void Reset()
{
    memset(tandyTestRAM, 0, sizeof(tandyTestRAM));
    tandyTestRAM[0xffffe] = 0xff;
    tandyTestRAM[0xfc000] = 0x21;
    WriteWord(0x40, 0x13, 624);
    WriteWord(0x40, 0x20, 0x100); // DOS list-of-lists: first MCB.
    SetBlock(0x100, 'M', 0x101, 111);
    SetBlock(0x170, 'M', 0, 0x97ff - 0x170 - 1);
    SetBlock(0x97ff, 'Z', 0x101, 0x400);
    memcpy(tandyTestRAM + 0x101 * 16 + 0x103, "TNDYRS01", 8);
    WriteWord(0x101, 0x10b, 0x9800);
    WriteWord(0x101, 0x10d, 0x400);
    currentPSP = 0x2000;
}

static void Expect(const char* name, bool expected)
{
    memcpy(originalRAM, tandyTestRAM, sizeof(tandyTestRAM));
    const bool actual = TandyVideoMemoryReady();
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s: expected %s, got %s\n", name,
            expected ? "safe" : "refusal", actual ? "safe" : "refusal");
        abort();
    }
    if (memcmp(originalRAM, tandyTestRAM, sizeof(tandyTestRAM)))
    {
        fprintf(stderr, "FAIL: %s: guard modified emulated memory\n", name);
        abort();
    }
    ++casesRun;
}

int main()
{
    Reset();
    Expect("recognized resident reservation", true);

    Reset();
    SetBlock(0x100, 'Z', 0, 0x9800 - 0x100 - 1);
    Expect("arena ends exactly at framebuffer", true);

    Reset();
    SetBlock(0x100, 'Z', 0, 0x9000 - 0x100 - 1);
    Expect("arena ends below framebuffer", true);

    Reset();
    SetBlock(0x100, 'Z', 0, 0x9c00 - 0x100 - 1);
    Expect("unreserved normal arena", false);

    Reset();
    currentPSP = 0x101;
    Expect("current process cannot be reservation owner", false);

    Reset();
    WriteWord(0x97ff, 1, 0);
    Expect("unowned high block", false);

    Reset();
    // Keep the chain valid so this specifically tests owner allocation size.
    SetBlock(0x100, 'M', 0x101, 110);
    SetBlock(0x16f, 'M', 0, 0x97ff - 0x16f - 1);
    Expect("wrong resident allocation size", false);

    Reset();
    WriteWord(0x100, 1, 0x200);
    Expect("low block has a different owner", false);

    Reset();
    tandyTestRAM[0x101 * 16 + 0x103] = 'X';
    Expect("incorrect resident marker", false);

    Reset();
    WriteWord(0x101, 0x10b, 0x9801);
    Expect("incorrect resident video segment", false);

    Reset();
    WriteWord(0x101, 0x10d, 0x3ff);
    Expect("incorrect resident paragraph count", false);

    Reset();
    tandyTestRAM[0x97ff0] = 'M';
    Expect("chain continues beyond conventional limit", false);

    Reset();
    WriteWord(0x97ff, 3, 0x401);
    Expect("high block extends beyond limit", false);

    Reset();
    WriteWord(0x100, 3, 0xffff);
    Expect("overflowing MCB size", false);

    Reset();
    tandyTestRAM[0x1000] = 'X';
    Expect("invalid MCB type", false);

    Reset();
    WriteWord(0x40, 0x13, 736);
    Expect("unproven expanded-base layout", false);

    Reset();
    WriteWord(0x40, 0x20, 0);
    Expect("invalid first MCB", false);

    Reset();
    tandyTestRAM[0xfc000] = 0;
    Expect("non-Tandy platform", false);

    assert(casesRun == 18);
    printf("PASS: %u production Tandy memory-guard cases; memory unchanged\n", casesRun);
    return 0;
}
