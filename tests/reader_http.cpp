// Production reader URL and HTTP client over real loopback TCP (host adapter).
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "../src/ReaderLink.h"
#include "../src/HTTP.h"
class LoopSocket : public NetworkTCPSocket {
    int fd; bool closed;
public:
    LoopSocket():fd(-1),closed(false) {}
    int Connect(NetworkAddress, int port) {
        fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0)return -1;
        sockaddr_in a={};a.sin_family=AF_INET;a.sin_port=htons(port);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        return connect(fd,(sockaddr*)&a,sizeof(a));
    }
    int Send(uint8_t* p,int n) { return send(fd,p,n>7?7:n,MSG_NOSIGNAL); }
    int Receive(uint8_t* p,int n) {
        int r=recv(fd,p,n>3?3:n,MSG_DONTWAIT);
        if(r<0 && (errno==EAGAIN || errno==EWOULDBLOCK))return 0;
        if(!r)closed=true;return r;
    }
    bool IsConnectComplete(){return true;}
    bool IsClosed(){return closed;}
    void Close(){if(fd>=0)close(fd);fd=-1;closed=true;}
};
class LoopNetwork : public NetworkDriver {
public:
    int live=0;
    int ResolveAddress(const char* name,NetworkAddress a,bool){assert(!strcmp(name,"127.0.0.1"));memset(a,0,4);return 0;}
    NetworkTCPSocket* CreateSocket(){++live;return new LoopSocket;}
    void DestroySocket(NetworkTCPSocket* p){p->Close();delete static_cast<LoopSocket*>(p);--live;}
};
NetworkDriver* Platform::network=NULL;
int main(int argc,char**argv) {
    assert(argc==2);unsetenv("HTTP_PROXY");LoopNetwork net;Platform::network=&net;
    {
        char prefix[128]; snprintf(prefix, sizeof(prefix), "http://127.0.0.1:%s/read.php?lg=en-us&a=", argv[1]);
        char url[512];
        assert(ReaderLink::Build(url, sizeof(url), prefix,
            "https://example.test/a%20b?q=x&n=two+words#section") == ReaderLink::Ready);
        HTTPRequest request;request.Open(HTTPRequest::Get,url,NULL);
        std::string result;
        for(int i=0;i<10000 && (request.GetStatus()==HTTPRequest::Connecting || request.GetStatus()==HTTPRequest::Downloading);++i) {
            request.Update();char bytes[11];size_t count=request.ReadData(bytes,sizeof(bytes));result.append(bytes,count);usleep(1000);
        }
        assert(request.GetStatus()==HTTPRequest::Finished);assert(result=="accepted");assert(net.live==0);
    }
    puts("PASS: production reader URL and HTTP client over loopback TCP; fragmented I/O and socket cleanup");
}
