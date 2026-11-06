#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "../src/HTTP.h"

static clock_t testTime = 0;
clock_t clock(void) noexcept { return testTime; }

struct FakeSocket : NetworkTCPSocket {
    std::string input, sent; size_t pos; bool closed, failSend; int stall, sendLimit;
    FakeSocket(const std::string& text) : input(text), pos(0), closed(false), failSend(false), stall(0), sendLimit(0) {}
    int Send(uint8_t* p, int n) { if(failSend) return -1; if(sendLimit && n>sendLimit) n=sendLimit; sent.append((char*)p,n); return n; }
    int Receive(uint8_t* p, int n) {
        if(stall) { --stall; return 0; }
        int amount = (int)(input.size()-pos); if(amount>n) amount=n;
        memcpy(p,input.data()+pos,amount); pos+=amount; return amount;
    }
    int Connect(NetworkAddress, int) { return 0; }
    bool IsConnectComplete() { return true; }
    bool IsClosed() { return closed; }
    void Close() { closed=true; }
};
struct FakeNetwork : NetworkDriver {
    std::vector<std::string> replies, requests; FakeSocket* current; int created, destroyed; bool failCreate;
    FakeNetwork() : current(NULL), created(0), destroyed(0), failCreate(false) {}
    int ResolveAddress(const char*, NetworkAddress, bool) { return 0; }
    NetworkTCPSocket* CreateSocket() {
        if(failCreate) return NULL;
        assert(created<(int)replies.size()); current=new FakeSocket(replies[created++]); return current;
    }
    void DestroySocket(NetworkTCPSocket* p) {
        FakeSocket* f=static_cast<FakeSocket*>(p); requests.push_back(f->sent); delete f; current=NULL; ++destroyed;
    }
};
NetworkDriver* Platform::network = NULL;
static void open(HTTPRequest& r, const char* url, HTTPRequest::RequestType type=HTTPRequest::Get, HTTPOptions* o=NULL) {
    r.Open(type,const_cast<char*>(url),o);
}
static std::string run(HTTPRequest& r) {
    std::string body;
    for(int i=0;i<200 && (r.GetStatus()==HTTPRequest::Connecting || r.GetStatus()==HTTPRequest::Downloading);++i) {
        r.Update(); char b[16]; size_t n=r.ReadData(b,sizeof(b)); body.append(b,n);
    }
    return body;
}
static void redirect(const char* base,const char* location,const char* expected,int code=302) {
    FakeNetwork net; Platform::network=&net;
    char status[64]; sprintf(status,"HTTP/1.1 %d Redirect\r\nLocation:",code);
    net.replies.push_back(std::string(status)+location+"\r\nContent-Length:0\r\n\r\n");
    net.replies.push_back("HTTP/1.1 200 OK\r\nContent-Length:2\r\n\r\nOK");
    HTTPRequest r; open(r,base); assert(run(r)=="OK"); assert(r.GetStatus()==HTTPRequest::Finished);
    assert(!strcmp(r.GetURL(),expected)); assert(net.created==2 && net.destroyed==2);
}
int main() {
    unsetenv("HTTP_PROXY");
    redirect("http://example/a/b","../c","http://example/c");
    redirect("http://example/a/b","/c","http://example/c");
    redirect("http://example/a/b?q=1","?q=2","http://example/a/b?q=2");
    redirect("http://example/a/b#old","#new","http://example/a/b#new");
    redirect("http://example","next","http://example/next");
    redirect("http://example?q=x","next","http://example/next");
    redirect("http://example/a/b","//other/c","http://other/c");
    redirect("http://example/a/b","http://other/c?q=/../x","http://other/c?q=/../x");
    redirect("http://example/a/b","../../../../c","http://example/c");
    redirect("http://example/a/b","..","http://example/");
    redirect("http://example/a/b",". ","http://example/a/");
    for(int code=301;code<=308;++code) { if(code==304 || code==305 || code==306) continue;
        FakeNetwork net; Platform::network=&net; char h[100]; sprintf(h,"HTTP/1.1 %d Redirect\r\nLocation: /next\r\n\r\n",code);
        net.replies.push_back(h); net.replies.push_back("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
        HTTPOptions opts; opts.postContentType="text/plain"; opts.contentData="abc";
        HTTPRequest r; open(r,"http://example/start",HTTPRequest::Post,&opts); run(r);
        assert(r.GetStatus()==HTTPRequest::Finished); assert(net.requests.size()==2);
        assert(net.requests[1].find(code>=307 ? "POST /next" : "GET /next")==0);
        assert((net.requests[1].find("abc")!=std::string::npos)==(code>=307));
    }
    { FakeNetwork net; Platform::network=&net; for(int i=0;i<6;++i) net.replies.push_back("HTTP/1.1 302 Redirect\r\nLocation:/loop\r\n\r\n");
      HTTPRequest r; open(r,"http://example/loop"); run(r); assert(r.GetStatus()==HTTPRequest::Error); assert(net.created==6 && net.destroyed==6);
      net.replies.push_back("HTTP/1.1 200 OK\r\nContent-Length:0\r\n\r\n"); open(r,"http://example/new"); run(r); assert(r.GetStatus()==HTTPRequest::Finished); }
    { FakeNetwork net; Platform::network=&net; net.failCreate=true; HTTPRequest r; open(r,"http://example/"); run(r); assert(r.GetStatus()==HTTPRequest::Error); }
    const char* bad[]={"http://", "http://example:0/", "http://example:65536/", "http://example:12x/", "http://example/a\r\nb"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) { HTTPRequest r; open(r,bad[i]); assert(r.GetStatus()==HTTPRequest::Error); }
    { HTTPRequest r; std::string huge="http://"+std::string(90,'a')+"/"; open(r,huge.c_str()); assert(r.GetStatus()==HTTPRequest::Error); }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back("HTTP/1.1 302 Redirect\r\nLocation: https://other/secure\r\n\r\n");
      HTTPRequest r; open(r,"http://example/"); run(r); assert(r.GetStatus()==HTTPRequest::UnsupportedHTTPS); assert(net.created==net.destroyed); }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back("HTTP/1.1 302 Redirect\r\nLocation: /"+std::string(600,'a')+"\r\n\r\n");
      HTTPRequest r; open(r,"http://example/"); run(r); assert(r.GetStatus()==HTTPRequest::Error); assert(net.created==net.destroyed); }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back("HTTP/1.1 302 Redirect\r\nLocation: "+std::string(490,'a')+"\r\n\r\n");
      HTTPRequest r; open(r,"http://example/long/path/to/page"); run(r); assert(r.GetStatus()==HTTPRequest::Error); assert(net.created==net.destroyed); }
    for(int known=0;known<2;++known) {
      FakeNetwork net; Platform::network=&net; net.replies.push_back(known ? "HTTP/1.1 200 OK\r\nContent-Length:9\r\n\r\nabc" : "HTTP/1.1 200 OK\r\n\r\nabc");
      HTTPRequest r; open(r,"http://example/"); assert(run(r)=="abc"); assert(r.GetStatus()==HTTPRequest::Downloading);
      char b[9]; assert(r.ReadData(b,0)==0); assert(r.GetStatus()==HTTPRequest::Downloading);
      net.current->closed=true; assert(r.ReadData(b,9)==0); assert(r.GetStatus()==(known ? HTTPRequest::Error : HTTPRequest::Finished)); assert(net.created==net.destroyed);
    }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back("HTTP/1.1 200 OK\r\nContent-Length:3\r\n\r\nabcEXCESS"); HTTPRequest r; open(r,"http://example/"); assert(run(r)=="abc"); assert(r.GetStatus()==HTTPRequest::Finished); }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length:1\r\n\r\n3\r\nabc\r\n0\r\n\r\n"); HTTPRequest r; open(r,"http://example/"); assert(run(r)=="abc"); assert(r.GetStatus()==HTTPRequest::Finished); assert(net.created==net.destroyed); }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back("HTTP/1.1 200"); HTTPRequest r; open(r,"http://example/"); run(r); net.current->closed=true; r.Update(); assert(r.GetStatus()==HTTPRequest::Error); assert(net.created==net.destroyed); }

    { FakeNetwork net; Platform::network=&net; net.replies.push_back(""); net.replies.push_back("HTTP/1.1 200 OK\r\nContent-Length:0\r\n\r\n");
      HTTPRequest r; open(r,"http://example/"); run(r); testTime += HTTP_RESPONSE_TIMEOUT + 1; r.Update(); run(r);
      assert(r.GetStatus()==HTTPRequest::Finished && net.created==2 && net.destroyed==2); }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back(""); net.replies.push_back("");
      HTTPRequest r; open(r,"http://example/"); run(r); testTime += HTTP_RESPONSE_TIMEOUT + 1; r.Update(); run(r);
      testTime += HTTP_RESPONSE_TIMEOUT + 1; r.Update(); assert(r.GetStatus()==HTTPRequest::Error && net.created==2 && net.destroyed==2); }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back(""); HTTPOptions o; o.contentData="abc"; o.postContentType="text/plain";
      HTTPRequest r; open(r,"http://example/",HTTPRequest::Post,&o); run(r); testTime += HTTP_RESPONSE_TIMEOUT + 1; r.Update();
      assert(r.GetStatus()==HTTPRequest::Error && net.created==1 && net.destroyed==1); }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back("H"); HTTPRequest r; open(r,"http://example/"); run(r);
      testTime += HTTP_RESPONSE_TIMEOUT + 1; r.Update(); assert(r.GetStatus()==HTTPRequest::Error && net.created==1 && net.destroyed==1); }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back("HTTP/1.1 200 OK\r\nContent-Length:0\r\n\r\n");
      HTTPRequest r; HTTPOptions o; std::string body(511,'x'); o.contentData=body.c_str(); o.postContentType="text/plain";
      open(r,"http://example/",HTTPRequest::Post,&o); r.Update(); r.Update(); net.current->sendLimit=7; run(r);
      assert(r.GetStatus()==HTTPRequest::Finished); const std::string& sent=net.requests[0]; size_t end=sent.find("\r\n\r\n");
      assert(sent.substr(end+4)==body); }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back(""); HTTPRequest r; open(r,"http://example/"); r.Update(); r.Update();
      net.current->failSend=true; run(r); assert(r.GetStatus()==HTTPRequest::Error && net.created==net.destroyed); }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back("HTTP/1.1 302 Redirect\r\nLocation: //other/x\r\n\r\n");
      HTTPRequest r; HTTPOptions o; o.headerParams="Authorization: secret"; open(r,"http://example/",HTTPRequest::Get,&o); run(r);
      assert(r.GetStatus()==HTTPRequest::Error && net.created==1 && net.destroyed==1); }
    const char* invalidBodies[]={"Transfer-Encoding:chunked\r\n\r\nZ\r\n", "Transfer-Encoding:chunked\r\n\r\n-1\r\n", "Content-Length:-1\r\n\r\n", "Content-Length:3x\r\n\r\n", "Content-Length:2\r\nContent-Length:3\r\n\r\n", "Transfer-Encoding:gzip\r\n\r\n"};
    for(unsigned i=0;i<sizeof(invalidBodies)/sizeof(*invalidBodies);++i) {
      FakeNetwork net; Platform::network=&net; net.replies.push_back(std::string("HTTP/1.1 200 OK\r\n")+invalidBodies[i]); HTTPRequest r;
      open(r,"http://example/"); run(r); assert(r.GetStatus()==HTTPRequest::Error && net.created==net.destroyed); }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back("HTTP/1.1 200 OK\r\nTransfer-Encoding:chunked\r\n\r\n0\r\n");
      HTTPRequest r; open(r,"http://example/"); run(r); assert(r.GetStatus()!=HTTPRequest::Finished); net.current->closed=true; r.Update(); assert(r.GetStatus()==HTTPRequest::Error); }

    { FakeNetwork net; Platform::network=&net; net.replies.push_back("HTTP/1.1 200 OK\r\nContent-Length:0\r\n\r\n");
      HTTPRequest r; setenv("HTTP_PROXY","proxy:8080",1); open(r,"http://example:8000/path"); run(r); unsetenv("HTTP_PROXY");
      assert(r.GetStatus()==HTTPRequest::Finished); assert(net.requests[0].find("Host: example:8000\r\n")!=std::string::npos); }
    puts("PASS: HTTP redirect, socket recovery, bounds and EOF regression cases");
}
