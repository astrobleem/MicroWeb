// Production DOS wrapper and mTCP lifetime methods, with host packet/IRQ mocks.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <vector>
#include "../src/DOS/DOSNet.h"
#include "../src/HTTP.h"
#define __interrupt
#define __far
#define TRACE_TCP(x)
#define TRACE_TCP_WARN(x)
#define TCP_RC_GOOD 0
#define TCP_RC_BAD -1
#define TCP_STATE_CLOSED 1
#define TCP_STATE_ESTABLISHED 5
#define TCP_STATE_TIME_WAIT 11
#define TCP_CODEBITS_ACK 16
#define PACKET_PROCESS_MULT(n) ((void)0)
typedef uint8_t IpAddr_t[4];
struct IpHeader { IpAddr_t ip_src; };
struct TcpHeader {
    uint16_t src, dst; uint32_t seqnum, acknum; unsigned bits;
    void setCodeBits(unsigned value) { bits = value; }
};
static bool allocationFailure;
static void* mTCP_malloc(size_t size, const char*) { return allocationFailure ? NULL : malloc(size); }
static unsigned removed, released, resets, cleared, parseCalls, stackStarts, stackEnds, vectorWrites;
static int parseResult, initResult;
static void oldBreak() {} static void oldControlC() {}
static void (*vectors[256])();
static void (*getvect(int v))() { return vectors[v]; }
static void setvect(int v, void (*handler)()) { vectors[v] = handler; ++vectorWrites; }
struct TcpSocket {
    uint8_t* rcvBuffer; uint16_t rcvBufSize; unsigned state, closeReason, pendingAccept;
    uint16_t srcPort, dstPort; IpAddr_t dstHost; uint32_t seqNum, ackNum;
    bool active; unsigned outgoing;
    TcpSocket() : rcvBuffer(NULL), rcvBufSize(0), state(TCP_STATE_CLOSED), closeReason(0),
        pendingAccept(0), srcPort(1234), dstPort(80), seqNum(123456), ackNum(654321), active(false), outgoing(0) { memset(dstHost, 0x42, 4); }
    int8_t setRecvBuffer(uint16_t);
    void destroy(); void abortConnection();
    void clearQueues() { outgoing = 0; ++cleared; }
    static void sendResetPacket(IpHeader* ip, TcpHeader* tcp, uint16_t len) {
        assert(ip->ip_src[0] == 0x42 && len == 0);
        assert(ntohs(tcp->src) == 80 && ntohs(tcp->dst) == 1234);
        assert(ntohl(tcp->seqnum) == 654321 && ntohl(tcp->acknum) == 123456);
        assert(tcp->bits == TCP_CODEBITS_ACK); ++resets;
    }
    int send(uint8_t*, int len) { return len; }
    int recv(uint8_t*, int) { return 0; }
    int connectNonBlocking(uint16_t, NetworkAddress, int) { active=true; state=TCP_STATE_ESTABLISHED; return 0; }
    bool isConnectComplete() { return state == TCP_STATE_ESTABLISHED; }
    bool isRemoteClosed() { return state != TCP_STATE_ESTABLISHED; }
};
static TcpSocket pool[2];
static bool inUse[2];
struct TcpSocketMgr {
    static TcpSocket* getSocket() {
        for (int n=0; n<2; ++n) if (!inUse[n]) { inUse[n]=true; pool[n]=TcpSocket(); return &pool[n]; }
        return NULL;
    }
    static int freeSocket(TcpSocket* p) {
        assert(!p->active && p->outgoing == 0 && p->rcvBuffer == NULL);
        assert(inUse[p-pool]); inUse[p-pool]=false; ++released; return 0;
    }
    static void makeInactive(TcpSocket* p) { p->active=false; ++removed; }
};
struct Utils {
    static int parseEnv() { ++parseCalls; return parseResult; }
    static int initStack(int, int, void(*cb)(), void(*cc)()) {
        ++stackStarts; setvect(0x1b, cb); setvect(0x23, cc); return initResult;
    }
    static void endStack() { assert(!inUse[0] && !inUse[1]); ++stackEnds; }
};
#define TCP_SOCKET_RING_SIZE 4
struct Arp { static void driveArp() {} };
struct Tcp { static void drivePackets() {} };
struct Dns { static void drivePendingQuery() {} static int resolve(const char*, NetworkAddress, int) { return 0; } };
HTTPRequest::HTTPRequest() : status(Stopped) {}
void HTTPRequest::Update() {}
void HTTPRequest::Stop() { status = Stopped; }
void Platform::FatalError(const char*, ...) { assert(!"Unexpected fatal error"); abort(); }
#include "runtime-mtcp.inc"
#include "runtime-dosnet.inc"
int main() {
    vectors[0x1b]=oldBreak; vectors[0x23]=oldControlC;
    parseResult=1;
    { DOSNetworkDriver net; net.Init(); net.Shutdown(); net.Shutdown(); }
    assert(vectorWrites==0 && stackStarts==0 && vectors[0x1b]==oldBreak && vectors[0x23]==oldControlC);
    parseResult=0; initResult=1;
    { DOSNetworkDriver net; net.Init(); assert(!net.IsConnected());
      assert(vectors[0x1b]==oldBreak && vectors[0x23]==oldControlC);
      unsigned writes=vectorWrites; net.Shutdown(); assert(writes==vectorWrites); }
    assert(stackEnds==0);
    initResult=0;
    { DOSNetworkDriver net; net.Init(); assert(net.IsConnected());
      allocationFailure=true; assert(net.CreateSocket()==NULL); allocationFailure=false;
      for (unsigned n=0;n<100;++n) {
        NetworkTCPSocket* s=net.CreateSocket(); assert(s); uint8_t address[4]={1,2,3,4};
        s->Connect(address,80); pool[0].outgoing=3;
        net.DestroySocket(s); assert(!inUse[0] && !pool[0].active && pool[0].rcvBuffer==NULL);
      }
      NetworkTCPSocket* s=net.CreateSocket(); assert(s); uint8_t address[4]={1,2,3,4}; s->Connect(address,80);
      // mTCP can destroy a reset/timed-out connection before the wrapper closes it.
      pool[0].destroy(); net.DestroySocket(s);
      assert(net.CreateSocket()); assert(net.CreateSocket()); assert(!net.CreateSocket());
      net.Shutdown(); unsigned writes=vectorWrites; net.Shutdown(); assert(writes==vectorWrites);
    }
    assert(stackEnds==1 && vectors[0x1b]==oldBreak && vectors[0x23]==oldControlC);
    assert(resets==100 && !inUse[0] && !inUse[1] && removed==104);
    printf("PASS: parse/init failures, vector restoration, 100 socket reuses, buffer ownership, resets, allocation failure and idempotent shutdown\n");
}
