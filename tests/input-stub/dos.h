#ifndef MICROWEB_TEST_INPUT_DOS_H
#define MICROWEB_TEST_INPUT_DOS_H
#include <stdint.h>
#define far
#define __interrupt
union REGS {
    struct { uint16_t ax, bx, cx, dx; } x, w;
    struct { uint8_t al, ah, bl, bh, cl, ch, dl, dh; } h;
};
void (*_dos_getvect(unsigned))();
void int86(int, REGS*, REGS*);
#endif
