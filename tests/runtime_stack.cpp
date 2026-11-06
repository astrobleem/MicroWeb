/* Real DOS/mTCP test: failed packet-driver discovery must not consume additional DOS memory. */
#include <stdio.h>
#include <dos.h>
#include "types.h"
#include "utils.h"
static void __interrupt __far OnBreak() {}
static unsigned long FreeDOSBytes() {
    unsigned paragraphs=0;
    _dos_allocmem(0xffff,&paragraphs);
    return (unsigned long)paragraphs*16;
}
int main() {
    FILE* result;
    unsigned long before, after;
    int failures=0;
    void (__interrupt __far* beforeBreak)() = _dos_getvect(0x1b);
    void (__interrupt __far* beforeControlC)() = _dos_getvect(0x23);
    void (__interrupt __far* beforeTimer)() = _dos_getvect(0x1c);
    freopen("ERROR.TXT","w",stderr);
    if (Utils::parseEnv()!=0) return 1;
    // Warm up library I/O and heap bookkeeping before measuring free DOS memory.
    if (Utils::initStack(2,4,OnBreak,OnBreak)==0) { Utils::endStack(); return 2; }
    before=FreeDOSBytes();
    for (int n=0;n<3;++n) {
        if (Utils::initStack(2,4,OnBreak,OnBreak)!=0) ++failures;
        else Utils::endStack();
    }
    after=FreeDOSBytes();
    result=fopen("STACK.TXT","w");
    fprintf(result,"failures=%d before=%lu after=%lu break=%d ctrlc=%d timer=%d\n",failures,before,after,
        _dos_getvect(0x1b)==beforeBreak,_dos_getvect(0x23)==beforeControlC,_dos_getvect(0x1c)==beforeTimer);
    fclose(result);
    return 0;
}
