// Production form encoding and HTTP over real loopback TCP (host adapter).
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "../src/Node.h"
#define private public
#include "../src/Nodes/Form.h"
#undef private
#include "../src/HTTP.h"
Node::Node(Type t) : styleHandle(0), type(t), isLayoutComplete(false) { anchor.x=anchor.y=size.x=size.y=0; }
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
    for(int test=0;test<3;++test) {
        FormNode::Data form;HiddenFieldData hidden("token","a&b=+% space");form.firstChild=&hidden;
        char largest[510];memset(largest,'x',509);largest[509]=0;
        if(test==1){hidden.name="v";hidden.value=largest;}
        form.method=test?FormNode::Data::Post:FormNode::Data::Get;
        char url[512];snprintf(url,sizeof(url),"http://127.0.0.1:%s/start%d?old=1#fragment",argv[1],test);
        URL address(url);char body[512];assert(FormNode::BuildSubmission(&form,address,body,NULL));
        if(test==1)assert(strlen(body)==511);
        HTTPOptions options;options.postContentType="application/x-www-form-urlencoded";options.contentData=body;
        HTTPRequest request;request.Open(test?HTTPRequest::Post:HTTPRequest::Get,address.url,test?&options:NULL);
        std::string result;
        for(int i=0;i<10000 && (request.GetStatus()==HTTPRequest::Connecting || request.GetStatus()==HTTPRequest::Downloading);++i) {
            request.Update();char bytes[11];size_t count=request.ReadData(bytes,sizeof(bytes));result.append(bytes,count);usleep(1000);
        }
        assert(request.GetStatus()==HTTPRequest::Finished);assert(result=="accepted");assert(net.live==0);
    }
    puts("PASS: production forms and HTTP over loopback TCP; GET, 511-byte POST/307 replay, POST/303 GET, partial send/receive, socket cleanup");
}
