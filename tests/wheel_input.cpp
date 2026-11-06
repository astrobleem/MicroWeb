#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
// Include the production implementations so their private assembly helper
// can be replaced on the host, and scroll target arithmetic tested directly.
#include "../src/DOS/DOSInput.cpp"
#include "../src/Interface.cpp"

VideoDriver* Platform::video;
DataPack Assets;
MouseCursorData* DataPack::GetMouseCursorData(MouseCursor::Type) { return NULL; }
static void SetMouseCursorASM(unsigned short*, uint16_t, uint16_t) {}

static unsigned char vectorByte;
static bool noVector, resetOK, wheelSignature, wheelCapability, untouchedProbe;
static int rawButtons, rawX, rawY, rawWheel, presses, releases, pendingKey;
static unsigned statusPolls, probes, interruptCalls, cases;
static std::vector<unsigned> calls;

void (*_dos_getvect(unsigned vector))()
{
    assert(vector == 0x33);
    return noVector ? NULL : reinterpret_cast<void(*)()>(&vectorByte);
}
int kbhit() { return pendingKey != 0; }
int getch() { int key = pendingKey; pendingKey = 0; return key; }
void int86(int vector, REGS* in, REGS* out)
{
    assert(vector == 0x33);
    ++interruptCalls;
    calls.push_back(in->x.ax);
    *out = *in;
    switch (in->x.ax)
    {
    case 0: out->x.ax = resetOK ? 0xffff : 0; break;
    case 0x11:
        ++probes;
        assert(in->x.bx == 0 && in->x.cx == 0 && in->x.dx == 0);
        if (!untouchedProbe) {
            out->x.ax = wheelSignature ? 0x574d : 0;
            out->x.cx = wheelCapability ? 1 : 0;
        }
        break;
    case 3:
        ++statusPolls;
        out->x.bx = (uint16_t)((rawWheel & 255) << 8 | rawButtons);
        out->x.cx = rawX; out->x.dx = rawY;
        rawWheel = 0;
        break;
    case 5:
        assert(in->x.bx == 0);
        out->x.bx = presses; presses = 0;
        out->x.cx = rawX; out->x.dx = rawY;
        break;
    case 6:
        assert(in->x.bx == 0);
        out->x.bx = releases; releases = 0;
        out->x.cx = rawX; out->x.dx = rawY;
        break;
    case 1: case 2: case 4: case 7: case 8: break;
    default: assert(!"Unexpected mouse call");
    }
}

struct TestVideo : VideoDriver {
    VideoModeInfo info;
    TestVideo() { memset(&info, 0, sizeof(info)); screenWidth = 640; screenHeight = 200; videoMode = &info; drawSurface = NULL; }
    void Init(VideoModeInfo*) {}
    void Shutdown() {}
};
struct TestSurface : DrawSurface {
    unsigned cursors;
    TestSurface() : DrawSurface(640, 200), cursors(0) {}
    void Clear() {}
    void HLine(DrawContext&, int, int, int, uint8_t) {}
    void VLine(DrawContext&, int, int, int, uint8_t) {}
    void FillRect(DrawContext&, int, int, int, int, uint8_t) {}
    void InvertRect(DrawContext&, int, int, int, int) {}
    void DrawString(DrawContext&, Font*, const char*, int, int, uint8_t, FontStyle::Type) {}
    void BlitImage(DrawContext&, Image*, int, int) {}
    void VerticalScrollBar(DrawContext&, int, int, int, int, int) {}
    void DrawCursor(MouseCursorData*, int, int) { ++cursors; }
};
static void reset()
{
    noVector = false; vectorByte = 0x90; resetOK = true;
    wheelSignature = wheelCapability = true; untouchedProbe = false;
    rawButtons = rawWheel = presses = releases = pendingKey = 0;
    rawX = 200; rawY = 100;
    statusPolls = probes = interruptCalls = 0; calls.clear();
    ++cases;
}
static void status(DOSInputDriver& input) { int b, x, y; input.GetMouseStatus(b, x, y); }

int main()
{
    TestVideo video; TestSurface surface;
    video.drawSurface = &surface; Platform::video = &video;
    int b, x, y;
    // Absent vectors, IRET stubs, and failed reset never probe or poll.
    for (int mode = 0; mode < 3; ++mode) {
        reset(); noVector = mode == 0; vectorByte = mode == 1 ? 0xcf : 0x90; resetOK = false;
        DOSInputDriver input; input.Init();
        input.GetMouseStatus(b, x, y);
        assert(b == 0 && x == 0 && y == 0 && input.GetMouseWheelDelta() == 0);
        assert(!input.HasInputPending() && probes == 0 && statusPolls == 0);
    }
    // Legacy drivers and API-without-wheel devices ignore high BX entirely.
    for (int mode = 0; mode < 3; ++mode) {
        reset(); untouchedProbe = mode == 0; wheelSignature = mode != 1; wheelCapability = mode != 2;
        DOSInputDriver input; input.Init(); assert(probes == 1);
        rawWheel = -1; rawButtons = 3; input.GetMouseStatus(b, x, y);
        assert(b == 3 && x == 200 && y == 100 && input.GetMouseWheelDelta() == 0);
        assert(!input.HasInputPending());
    }
    // Signed wheel notches queue across all status readers; exactly-once drain.
    reset(); DOSInputDriver input; input.Init(); assert(calls[0] == 0 && calls[1] == 0x11);
    for (int d = -128; d <= 127; ++d) {
        rawWheel = d; rawButtons = 5; input.GetMouseStatus(b, x, y);
        assert(b == 5 && input.GetMouseWheelDelta() == d && input.GetMouseWheelDelta() == 0);
    }
    rawWheel = 2; input.RefreshMouse(); rawWheel = -1; status(input);
    assert(input.GetMouseWheelDelta() == 1 && surface.cursors > 0);
    // Loading can check repeatedly without consuming the pending scroll or click.
    rawWheel = -3; presses = 1; assert(input.HasInputPending());
    assert(input.HasInputPending()); assert(input.GetMouseWheelDelta() == -3);
    assert(input.HasInputPending()); assert(input.GetMouseButtonPress(x, y));
    assert(x == 200 && y == 100 && !input.GetMouseButtonPress(x, y));
    assert(!input.HasInputPending());
    pendingKey = 'a'; assert(input.HasInputPending()); assert(input.GetKeyPress() == 'a');
    releases = 1; assert(input.GetMouseButtonRelease(x, y)); assert(!input.GetMouseButtonRelease(x, y));
    video.screenWidth = 320; rawWheel = 1; input.GetMouseStatus(b, x, y);
    assert(x == 100 && input.GetMouseWheelDelta() == 1);
    // DOS int-sized queues saturate, never wrap and reverse direction.
    for (int n = 0; n < 300; ++n) { rawWheel = 127; status(input); }
    assert(input.GetMouseWheelDelta() == 32767);
    for (int n = 0; n < 300; ++n) { rawWheel = -128; status(input); }
    assert(input.GetMouseWheelDelta() == -32767);
    rawWheel = 2; status(input); input.Init(); assert(input.GetMouseWheelDelta() == 0);
    // The exact production UI calculation clamps to loaded page bounds.
    assert(WheelScrollTarget(0, 1000, 150, 1) == 24);
    assert(WheelScrollTarget(100, 1000, 150, -1) == 76);
    assert(WheelScrollTarget(0, 1000, 150, -1) == 0);
    assert(WheelScrollTarget(840, 1000, 150, 2) == 850);
    assert(WheelScrollTarget(0, 100, 150, 1) == 0);
    assert(WheelScrollTarget(32000, 32767, 200, 32767) == 32567);
    assert(WheelScrollTarget(32000, 32767, 200, -32767) == 0);
    assert(WheelScrollTarget(0, 32767, 200, 32767) == 32567);
    printf("PASS: %u driver scenarios, 256 signed deltas, queue/cursor/click/loading checks and UI bounds\n", cases);
}
