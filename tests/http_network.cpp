#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "../src/HTTP.h"

static clock_t testTime = 0;
clock_t clock(void) noexcept { return testTime; }

struct FakeSocket : NetworkTCPSocket {
    std::string input, sent; size_t pos; bool closed, failSend; int stall, sendLimit, port;
    FakeSocket(const std::string& text) : input(text), pos(0), closed(false), failSend(false), stall(0), sendLimit(0), port(0) {}
    int Send(uint8_t* p, int n) { if(failSend) return -1; if(sendLimit && n>sendLimit) n=sendLimit; sent.append((char*)p,n); return n; }
    int Receive(uint8_t* p, int n) {
        if(stall) { --stall; return 0; }
        int amount = (int)(input.size()-pos); if(amount>n) amount=n;
        memcpy(p,input.data()+pos,amount); pos+=amount; return amount;
    }
    int Connect(NetworkAddress, int p) { port=p; return 0; }
    bool IsConnectComplete() { return true; }
    bool IsClosed() { return closed; }
    void Close() { closed=true; }
};
struct FakeNetwork : NetworkDriver {
    std::vector<std::string> replies, requests, hosts; std::vector<int> ports; FakeSocket* current; int created, destroyed; bool failCreate;
    FakeNetwork() : current(NULL), created(0), destroyed(0), failCreate(false) {}
    int ResolveAddress(const char* name, NetworkAddress, bool) { hosts.push_back(name); return 0; }
    NetworkTCPSocket* CreateSocket() {
        if(failCreate) return NULL;
        assert(created<(int)replies.size()); current=new FakeSocket(replies[created++]); return current;
    }
    void DestroySocket(NetworkTCPSocket* p) {
        FakeSocket* f=static_cast<FakeSocket*>(p); requests.push_back(f->sent); ports.push_back(f->port); delete f; current=NULL; ++destroyed;
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
    unsetenv("HTTP_PROXY"); unsetenv("MICROWEB_HTTPS_PROXY");
    unsetenv("HTTP_USER_AGENT");
    const char* agentValues[] = { "(DOS; Tandy 1000)", "", "evil\r\nX-Injected: yes", "bad\tvalue", "bad\177value" };
    for (unsigned i = 0; i < sizeof(agentValues) / sizeof(agentValues[0]); ++i) {
        setenv("HTTP_USER_AGENT", agentValues[i], 1);
        FakeNetwork net; Platform::network = &net;
        net.replies.push_back("HTTP/1.1 200 OK\r\nContent-Length:0\r\n\r\n");
        HTTPRequest r; open(r, "http://example/"); run(r);
        assert(r.GetStatus() == HTTPRequest::Finished && net.requests.size() == 1);
        const std::string expected = i == 0 ? "User-Agent: MicroWeb 2.1 (DOS; Tandy 1000)\r\n" : "User-Agent: MicroWeb 2.1\r\n";
        assert(net.requests[0].find(expected) != std::string::npos);
        assert(net.requests[0].find("X-Injected:") == std::string::npos);
    }
    { std::string huge(121, 'x'); setenv("HTTP_USER_AGENT", huge.c_str(), 1);
      FakeNetwork net; Platform::network = &net; net.replies.push_back("HTTP/1.1 200 OK\r\nContent-Length:0\r\n\r\n");
      HTTPRequest r; open(r, "http://example/"); run(r); assert(r.GetStatus() == HTTPRequest::Finished);
      assert(net.requests[0].find("User-Agent: MicroWeb 2.1\r\n") != std::string::npos); }
    unsetenv("HTTP_USER_AGENT");
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
    // Opt-in gateway routing: no TLS/CONNECT on DOS, and HTTP_PROXY alone
    // must not send an HTTPS request (or its POST body) anywhere.
    { FakeNetwork net; Platform::network=&net; setenv("HTTP_PROXY","ordinary:8080",1);
      HTTPOptions o; o.contentData="private=body"; o.postContentType="text/plain";
      HTTPRequest r; open(r,"https://origin.example/secure",HTTPRequest::Post,&o);
      assert(r.GetStatus()==HTTPRequest::UnsupportedHTTPS && net.created==0 && net.hosts.empty());
      assert(!strcmp(r.GetURL(),"https://origin.example/secure")); unsetenv("HTTP_PROXY"); }
    setenv("MICROWEB_HTTPS_PROXY","gateway:8081",1);
    { FakeNetwork net; Platform::network=&net; net.replies.push_back("HTTP/1.1 200 OK\r\nContent-Length:2\r\n\r\nOK");
      setenv("HTTP_PROXY","ordinary:8080",1); HTTPRequest r;
      open(r,"https://origin.example:8443/a?q=1#local");
      assert(!strcmp(r.GetStatusString(),HTTPS_PROXY_WARNING));
      assert(run(r)=="OK" && r.GetStatus()==HTTPRequest::Finished);
      assert(!strcmp(r.GetURL(),"https://origin.example:8443/a?q=1#local"));
      assert(net.hosts.size()==1 && net.hosts[0]=="gateway" && net.ports[0]==8081);
      assert(net.requests[0].find("GET https://origin.example:8443/a?q=1 HTTP/1.1\r\n")==0);
      assert(net.requests[0].find("Host: origin.example:8443\r\n")!=std::string::npos);
      assert(net.requests[0].find("#local")==std::string::npos && net.requests[0].find("CONNECT")==std::string::npos);
      unsetenv("HTTP_PROXY"); }
    redirect("https://origin.example/a/b","../next","https://origin.example/next");
    redirect("https://origin.example/a/b","//other.example/next","https://other.example/next");
    redirect("https://origin.example/a?old","?new","https://origin.example/a?new");
    redirect("http://origin.example/a","https://other.example/next","https://other.example/next");
    { FakeNetwork net; Platform::network=&net;
      net.replies.push_back("HTTP/1.1 307 Redirect\r\nLocation: //origin.example/next\r\n\r\n");
      net.replies.push_back("HTTP/1.1 200 OK\r\nContent-Length:0\r\n\r\n");
      HTTPOptions o; o.contentData="a=1&b=2"; o.postContentType="application/x-www-form-urlencoded";
      HTTPRequest r; open(r,"https://origin.example/start",HTTPRequest::Post,&o); run(r);
      assert(r.GetStatus()==HTTPRequest::Finished && net.requests.size()==2);
      for(int i=0;i<2;++i) { assert(net.hosts[i]=="gateway" && net.ports[i]==8081);
        assert(net.requests[i].find("POST https://origin.example/")==0);
        assert(net.requests[i].find("\r\n\r\na=1&b=2")!=std::string::npos); } }
    { FakeNetwork net; Platform::network=&net; net.replies.push_back("HTTP/1.1 307 Redirect\r\nLocation: http://origin.example/plain\r\n\r\n");
      HTTPOptions o; o.contentData="private=body"; o.postContentType="text/plain";
      HTTPRequest r; open(r,"https://origin.example/secure",HTTPRequest::Post,&o); run(r);
      assert(r.GetStatus()==HTTPRequest::Error && net.created==1 && net.destroyed==1);
      assert(!strcmp(r.GetStatusString(),"Blocked HTTPS to HTTP redirect"));
      assert(!strcmp(r.GetURL(),"https://origin.example/secure")); }
    // Invalid origins are rejected even though the socket endpoint is a proxy.
    const char* badHTTPS[]={"https://", "https://user@origin/", "https://origin:0/", "https://origin:65536/",
      "https://origin:+80/", "https://origin:80x/", "https://origin:/", "https://origin:80:90/"};
    for(unsigned i=0;i<sizeof(badHTTPS)/sizeof(*badHTTPS);++i) {
      FakeNetwork net; Platform::network=&net; HTTPRequest r; open(r,badHTTPS[i]);
      assert(r.GetStatus()==HTTPRequest::Error && net.created==0 && net.hosts.empty()); }
    const char* badProxies[]={"http://gateway:80", "https://gateway:80", "user@gateway:80", "gateway:0",
      "gateway:65536", "gateway:+80", "gateway:", "gateway/path", "gateway:80:90", "gateway\r\nInjected:1"};
    for(unsigned i=0;i<sizeof(badProxies)/sizeof(*badProxies);++i) {
      FakeNetwork net; Platform::network=&net; setenv("MICROWEB_HTTPS_PROXY",badProxies[i],1);
      HTTPRequest r; open(r,"https://origin.example/");
      assert(r.GetStatus()==HTTPRequest::Error && net.created==0 && net.hosts.empty()); }
    { FakeNetwork net; Platform::network=&net; setenv("MICROWEB_HTTPS_PROXY","",1);
      HTTPRequest r; open(r,"https://origin.example/"); assert(r.GetStatus()==HTTPRequest::UnsupportedHTTPS && net.created==0); }
    // Gateway failure must not cause a direct HTTPS or HTTP fallback.
    { FakeNetwork net; Platform::network=&net; setenv("MICROWEB_HTTPS_PROXY","gateway:8081",1); net.failCreate=true;
      HTTPRequest r; open(r,"https://origin.example/"); run(r);
      assert(r.GetStatus()==HTTPRequest::Error && net.hosts.size()==1 && net.hosts[0]=="gateway"); }
    unsetenv("MICROWEB_HTTPS_PROXY");
    puts("PASS: HTTP redirects, socket recovery, bounds, EOF and explicit plaintext HTTPS gateway routing");
}
