#ifndef _TGA2VID_H_
#define _TGA2VID_H_

#include <stdint.h>

// Read-only identification. Older Tandys, VGA/RLX, and generic DOSBox Tandy
// emulation do not pass the Video II BIOS configuration-table check.
bool Tga2Detected();

class Tga2Video
{
public:
    Tga2Video();
    bool Init();
    void Shutdown();

private:
    bool Refuse(const char* reason);
    bool ReserveFramebuffer();
    void SetRegisters();

    int originalMode;
    bool registersChanged;
    uint16_t allocation;
    uint16_t framebuffer;
};

#endif
