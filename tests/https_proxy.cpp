// Production LoadTask and HTTP gateway protocol over real loopback TCP.
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "../src/App.h"
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
    int live=0; int requests=0; int sockets=0;
    HTTPRequest* CreateRequest(){++requests;return new HTTPRequest;}
    void DestroyRequest(HTTPRequest* p){--requests;delete p;}
    int ResolveAddress(const char* name,NetworkAddress a,bool){assert(!strcmp(name,"127.0.0.1"));memset(a,0,4);return 0;}
    NetworkTCPSocket* CreateSocket(){++live;++sockets;return new LoopSocket;}
    void DestroySocket(NetworkTCPSocket* p){p->Close();delete static_cast<LoopSocket*>(p);--live;}
};
NetworkDriver* Platform::network=NULL;
int main(int argc,char** argv) {
    assert(argc==2); unsetenv("MICROWEB_HTTPS_PROXY");
    setenv("HTTP_PROXY","unused.invalid:9999",1);
    LoopNetwork net; Platform::network=&net;
    { LoadTask task; task.Load(HTTPRequest::Get,"https://origin.invalid/private");
      assert(task.type==LoadTask::RemoteFile && task.request);
      assert(!strcmp(task.GetURL(),"https://origin.invalid/private"));
      assert(task.request->GetStatus()==HTTPRequest::UnsupportedHTTPS && !net.sockets);
      task.Stop(); assert(!net.requests); }
    char proxy[80]; snprintf(proxy,sizeof(proxy),"127.0.0.1:%s",argv[1]);
    setenv("MICROWEB_HTTPS_PROXY",proxy,1);
    for(int test=0;test<3;++test) {
        LoadTask task; HTTPOptions o; o.postContentType="application/x-www-form-urlencoded"; o.contentData="a=1%26b%3D2";
        task.Load(test?HTTPRequest::Post:HTTPRequest::Get,
            test==0?"https://origin.invalid:8443/start?q=hello%20world#local":
            test==1?"https://origin.invalid/post":"https://origin.invalid/downgrade",test?&o:NULL);
        assert(task.request && task.request->GetStatus()==HTTPRequest::Connecting);
        assert(!strcmp(task.request->GetStatusString(),HTTPS_PROXY_WARNING));
        std::string result;
        for(int i=0;i<10000 && task.IsBusy();++i) {
            task.request->Update();
            if(task.HasContent()) { char bytes[11];size_t count=task.GetContent(bytes,sizeof(bytes));result.append(bytes,count); }
            usleep(1000);
        }
        assert(task.request->GetStatus()==(test==2?HTTPRequest::Error:HTTPRequest::Finished));
        assert(result==(test==2?"":"gateway response"));
        assert(!strcmp(task.GetURL(),test==0?"https://other.invalid/final?q=ok":
            test==1?"https://origin.invalid/accepted":"https://origin.invalid/downgrade"));
        task.Stop(); assert(net.live==0 && net.requests==0);
    }
    assert(net.sockets==5);
    puts("PASS: production LoadTask + HTTP via loopback gateway; no implicit downgrade, absolute HTTPS targets, POST/307, scheme-relative redirect, downgrade block, cleanup");
}
