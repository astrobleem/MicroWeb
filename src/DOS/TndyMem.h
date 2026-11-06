#ifndef _TNDYMEM_H_
#define _TNDYMEM_H_

// Read-only safety check, before entering BIOS video mode 09h. Prints the
// reason and returns false if DOS can still allocate Tandy framebuffer RAM.
bool TandyVideoMemoryReady();

#endif
