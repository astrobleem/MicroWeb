#ifndef MICROWEB_TGA2_TEST_DOS_H
#define MICROWEB_TGA2_TEST_DOS_H
#include <stdint.h>
#define far
void* Tga2TestAddress(unsigned segment, unsigned offset);
#define MK_FP(segment, offset) Tga2TestAddress(segment, offset)
union REGS {
    struct { uint16_t ax, bx, cx, dx, si, di, cflag; } x;
    struct { uint8_t al, ah, bl, bh, cl, ch, dl, dh; } h;
};
struct SREGS { uint16_t es; };
void segread(SREGS* segments);
void int86(int interrupt, REGS* input, REGS* output);
void int86x(int interrupt, REGS* input, REGS* output, SREGS* segments);
unsigned _dos_allocmem(unsigned paragraphs, unsigned* segment);
unsigned _dos_freemem(unsigned segment);
unsigned _dos_setblock(unsigned paragraphs, unsigned segment, unsigned* available);
#endif
