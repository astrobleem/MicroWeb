#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <vector>
#include "../src/Platform.h"
#include "../src/VidModes.h"
#define far
#define MK_FP(s,o) ((void*)0)
union REGS { struct { uint16_t ax,bx,cx,dx; } x; struct { uint8_t al,ah,bl,bh,cl,ch,dl,dh; } h; };
static std::vector<int> events;
static bool rejectMode, failVideo;
static unsigned networkInits, networkUpdates, inputUpdates, memoryShutdowns;
static void (*exitCallback)();
static int CaptureAtExit(void (*callback)()) { exitCallback=callback; return 0; }
struct ExitError {};
static void RuntimeExit(int) { throw ExitError(); }
#define atexit CaptureAtExit
#define exit RuntimeExit
void int86(int, REGS*, REGS* out) { out->x.ax=0x12; out->x.bx=0; }
bool Tga2Detected() { return false; }
struct TestVideo : VideoDriver {
    void Init(VideoModeInfo*) { events.push_back(2); if (failVideo) Platform::FatalError("test fatal initialization failure"); }
    void Shutdown() { events.push_back(5); }
};
typedef TestVideo BIOSVideoDriver;
typedef TestVideo HerculesDriver;
struct DOSInputDriver : InputDriver {
    void Init() { events.push_back(3); }
    void Shutdown() { events.push_back(4); }
    void Update() { ++inputUpdates; }
    void HideMouse() {} void ShowMouse() {} void SetMouseCursor(MouseCursor::Type) {}
    void GetMouseStatus(int&,int&,int&) {} void SetMousePosition(int,int) {}
    bool GetMouseButtonPress(int&,int&) { return false; }
    bool GetMouseButtonRelease(int&,int&) { return false; }
};
struct DOSNetworkDriver : NetworkDriver {
    void Init() { events.push_back(1); ++networkInits; }
    void Shutdown() { events.push_back(6); }
    void Update() { ++networkUpdates; }
};
struct Allocator { void Shutdown() { events.push_back(7); ++memoryShutdowns; } };
struct MemoryManager { static Allocator pageBlockAllocator; };
Allocator MemoryManager::pageBlockAllocator;
struct App {
    struct Config { bool invertScreen; }; static Config config;
    bool closed; App() : closed(false) {}
    static App& Get() { static App app; return app; }
    void Close() { closed=true; }
};
App::Config App::config;
volatile uint8_t CtrlBreakDetected;
VideoModeInfo VideoModeList[21];
VideoModeInfo* ShowVideoModePicker(int) { return rejectMode ? NULL : VideoModeList; }
int GetNumVideoModes() { return 21; }
#include "runtime-platform.inc"
bool Find6845(int) { return false; }
bool DetectHercules() { return false; }
unsigned int GetAmstradROSVersion() { return 0; }
static void reset() {
    shutdownComplete=false; Platform::video=NULL; Platform::network=NULL; Platform::input=NULL;
    events.clear(); exitCallback=NULL; CtrlBreakDetected=0; failVideo=false;
}
int main() {
    char exe[]="MICROWEB.EXE", mode[]="-video=a"; char* args[]={exe,mode};
    reset(); rejectMode=true;
    assert(!Platform::Init(1,args)); assert(networkInits==0 && exitCallback==Platform::Shutdown);
    exitCallback(); assert(events==std::vector<int>({7})); exitCallback(); assert(events.size()==1);
    reset(); rejectMode=false;
    assert(Platform::Init(2,args)); assert(events==std::vector<int>({1,2,3}));
    Platform::Update(); assert(networkUpdates==1 && inputUpdates==1);
    CtrlBreakDetected=1; Platform::Update(); assert(App::Get().closed && networkUpdates==1);
    Platform::Shutdown(); assert(events==std::vector<int>({1,2,3,4,5,6,7})); exitCallback(); assert(events.size()==7);
    reset(); failVideo=true;
    try { Platform::Init(2,args); assert(!"Expected fatal initialization"); } catch (const ExitError&) {}
    assert(events==std::vector<int>({1,2,5,6,7})); exitCallback(); assert(events.size()==5);
    puts("PASS: mode cancellation before network hooks, normal/fatal/partial cleanup, exit callback, Ctrl-Break and double shutdown");
}
