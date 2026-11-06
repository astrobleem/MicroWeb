#ifndef MICROWEB_TEST_DOS_H
#define MICROWEB_TEST_DOS_H

// Minimal host replacement for the DOS interface used by TndyMem.cpp.
// Only tests compiled with -Itests/dos-stub see this header.
#include <stdint.h>

#define far
extern uint8_t tandyTestRAM[1024 * 1024];
#define MK_FP(segment, offset) \
    (tandyTestRAM + (unsigned long)(segment) * 16 + (offset))

union REGS
{
    struct { uint16_t ax, bx, cx, dx; } x;
    struct { uint8_t al, ah, bl, bh, cl, ch, dl, dh; } h;
};
struct SREGS { uint16_t es; };

void segread(SREGS* segments);
void int86(int interrupt, REGS* input, REGS* output);
void int86x(int interrupt, REGS* input, REGS* output, SREGS* segments);

#endif
