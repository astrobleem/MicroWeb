/* Guest-side child-process test: DOS must recover all installed IRQ vectors. */
#include <dos.h>
#include <process.h>
#include <stdio.h>
int main(int argc, char** argv) {
    void (__interrupt __far *beforeBreak)() = _dos_getvect(0x1b);
    void (__interrupt __far *beforeControlC)() = _dos_getvect(0x23);
    void (__interrupt __far *beforeTimer)() = _dos_getvect(0x1c);
    int rc;
    FILE* result;
    if (argc < 2) return 1;
    rc = spawnl(P_WAIT, argv[1], argv[1], "-video=a", argc > 2 ? argv[2] : "LOCAL.HTM", NULL);
    result = fopen("RESULT.TXT", "w");
    fprintf(result, "child=%d break=%d ctrlc=%d timer=%d\n", rc,
        _dos_getvect(0x1b) == beforeBreak, _dos_getvect(0x23) == beforeControlC,
        _dos_getvect(0x1c) == beforeTimer);
    fclose(result);
    return 0;
}
