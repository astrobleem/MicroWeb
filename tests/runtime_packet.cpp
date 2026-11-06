// Compile the real packet-buffer allocator and stack init/cleanup paths.
// Hardware/module hooks are mocked; allocations are real and accounted for.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define __interrupt
#define __far
#define far
#define __SMALL__
#define NOTRACE
#define COMPILE_TCP
#define COMPILE_DNS
#define IP_FRAGMENTS_ON
#define PACKET_BUFFERS 20
#define PACKET_BUFFER_LEN 1514
static uint8_t* Buffer_fs[PACKET_BUFFERS];
static uint8_t Buffer_fs_index, Buffer_first, Buffer_next, Buffer_lowFreeCount;
static uint8_t* Buffer_packetBeingCopied;
static void* BufferMemPtr;
static void* liveAllocation;
static bool failAllocation;
static unsigned allocations, frees, bytesLive, traceStarts, traceEnds, vectorWrites;
static unsigned driverReleases, timerStarts, timerStops, socketStops, transmitStops, dnsStops, ipStops;
static int packetResult, registrationResult, ipResult, socketResult, transmitResult, dnsResult;
static bool driverActive;
static uint16_t unusedVector[2];
#define MK_FP(s,o) unusedVector
static void* mTCP_malloc(size_t length, const char*) {
    if (failAllocation) return NULL;
    assert(!liveAllocation); liveAllocation=malloc(length); assert(liveAllocation);
    bytesLive=length; ++allocations; return liveAllocation;
}
static void TrackedFree(void* pointer) {
    assert(pointer==liveAllocation); ++frees; bytesLive=0; liveAllocation=NULL; free(pointer);
}
#define free TrackedFree
#include "runtime-packet.inc"
#undef free
static const char* InitErrorMsg="Initialization failed: %s\n";
static uint8_t packetInt=0x60, packetInt2;
static uint8_t MyEthAddr[6];
static void OldBreak() {} static void OldC() {} static void NewBreak() {} static void NewC() {}
static void (*oldCtrlBreakHandler)();
static void (*vectors[256])();
static void (*getvect(int v))() { return vectors[v]; }
static void setvect(int v,void (*handler)()) { vectors[v]=handler; ++vectorWrites; }
static void Trace_beginTracing() { ++traceStarts; }
static void Trace_endTracing() { ++traceEnds; }
static int Packet_init(uint8_t) { driverActive=packetResult==0; return packetResult; }
static void Packet_get_addr(uint8_t*) {}
static int Packet_registerEtherType(unsigned,void(*)(uint8_t*,uint16_t)) { return registrationResult; }
static void Packet_release_type() { assert(driverActive); driverActive=false; ++driverReleases; }
static void Timer_start() { ++timerStarts; }
static void Timer_stop() { ++timerStops; }
struct Ip {
    static void process(uint8_t*,uint16_t) {}
    static int initForReassembly() { return ipResult; }
    static void reassemblyStop() { ++ipStops; }
};
struct TcpSocketMgr {
    static int init(uint8_t) { return socketResult; }
    static void stop() { ++socketStops; }
};
struct TcpBuffer {
    static int init(uint8_t) { return transmitResult; }
    static void stop() { ++transmitStops; }
};
struct Dns {
    static int init() { return dnsResult; }
    static void stop() { ++dnsStops; }
};
#define _HEAPOK 0
static int _heapchk() { return _HEAPOK; }
struct Utils {
    static int8_t initStack(uint8_t,uint8_t,void(*)(),void(*)());
    static void endStack2(bool);
    static void endStack() { endStack2(false); }
};
#include "runtime-initstack.inc"
int main() {
    // Direct allocator ownership: offsets stay inside the real allocation;
    // stop is idempotent and initialization can subsequently allocate again.
    for (unsigned n=0;n<3;++n) {
        assert(Buffer_init()==0 && BufferMemPtr==liveAllocation);
        assert(bytesLive==20*1514 && Buffer_fs_index==0);
        for (unsigned b=0;b<PACKET_BUFFERS;++b)
            assert(Buffer_fs[b]==static_cast<uint8_t*>(liveAllocation)+b*1514);
        Buffer_startReceiving(); assert(Buffer_fs_index==20);
        Buffer_stopReceiving(); Buffer_stop(); Buffer_stop();
        assert(!BufferMemPtr && !bytesLive && allocations==frees);
    }
    vectors[0x1b]=OldBreak; vectors[0x23]=OldC;
    failAllocation=true;
    assert(Utils::initStack(2,4,NewBreak,NewC)==-1);
    assert(!bytesLive && vectorWrites==0 && traceStarts==traceEnds);
    failAllocation=false; packetResult=-1;
    assert(Utils::initStack(2,4,NewBreak,NewC)==-1);
    assert(!bytesLive && allocations==frees && vectorWrites==0 && driverReleases==0 && timerStops==0);
    assert(vectors[0x1b]==OldBreak && vectors[0x23]==OldC && traceStarts==traceEnds);
    // Fail after each later initialization stage, exercising real endStack.
    packetResult=0;
    int* failures[]={&registrationResult,&ipResult,&socketResult,&transmitResult,&dnsResult};
    for (unsigned n=0;n<5;++n) {
        *failures[n]=-1;
        assert(Utils::initStack(2,4,NewBreak,NewC)==-1);
        assert(!bytesLive && allocations==frees && !driverActive && vectors[0x1b]==OldBreak);
        assert(traceStarts==traceEnds); *failures[n]=0;
    }
    assert(Utils::initStack(2,4,NewBreak,NewC)==0 && bytesLive==20*1514 && Buffer_fs_index==20);
    Utils::endStack();
    assert(!bytesLive && allocations==frees && !driverActive && vectors[0x1b]==OldBreak);
    assert(driverReleases==6 && traceStarts==traceEnds);
    printf("PASS: real 30,280-byte packet allocation reclaimed across OOM, absent driver, five later failures, normal teardown and repeated buffer stops\n");
}
