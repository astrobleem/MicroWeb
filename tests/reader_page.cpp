// Exercise the actual HTTPS error-page entry point with UI/network capture.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#define private public
#include "../src/App.h"
#undef private

static std::string html, address, title;
static bool finished;
LinearAllocator MemoryManager::pageAllocator;
NetworkDriver* Platform::network = NULL;
Page::Page(App& a) : layout(*this), app(a) {}
void Page::Reset() {}
void Page::SetTitle(const char* s) { ::title = s; }
Layout::Layout(Page& p) : page(p), cursorStack(MemoryManager::pageAllocator), paramStack(MemoryManager::pageAllocator) {}
PageRenderer::PageRenderer(App& a) : app(a) {}
void PageRenderer::Reset() {}
void PageRenderer::RefreshAll() {}
AppInterface::AppInterface(App& a) : app(a) {}
void AppInterface::Reset() {}
void AppInterface::UpdateAddressBar(const URL& u) { address = u.url; }
HTMLParser::HTMLParser(Page& p) : page(p), contextStack(MemoryManager::pageAllocator) {}
void HTMLParser::Reset() { html.clear(); finished = false; }
void HTMLParser::Write(const char* s) { html += s; }
void HTMLParser::Finish() { finished = true; }
StylePool& StylePool::Get() { static StylePool styles; return styles; }
HTTPRequest::HTTPRequest() {}
void HTTPRequest::Stop() { url = "destroyed"; }
class FakeNetwork : public NetworkDriver {
public:
    int destroyed;
    FakeNetwork() : destroyed(0) {}
    HTTPRequest* CreateRequest() { assert(false); return NULL; }
    void DestroyRequest(HTTPRequest* request) { ++destroyed; delete request; }
};

int main()
{
    FakeNetwork network; Platform::network = &network;
    App app;
    const char* target = "https://example.test/final?one=1&two=2#frag";
    for (int test = 0; test < 4; ++test)
    {
        app.pageLoadTask.type = LoadTask::RemoteFile;
        app.pageLoadTask.url = "http://example.test/start";
        app.pageLoadTask.request = new HTTPRequest;
        app.pageLoadTask.request->url = target;
        if (!test) unsetenv("MICROWEB_READER");
        if (test == 1) setenv("MICROWEB_READER", "http://reader.test/?lg=en-us&a=", 1);
        if (test == 2) setenv("MICROWEB_READER", "https://reader.test/?a=", 1);
        if (test == 3) { setenv("MICROWEB_READER", "http://reader.test/?a=", 1); std::string huge="https://example.test/"+std::string(450,'%'); app.pageLoadTask.request->url=huge.c_str(); }
        std::string expectedTarget = app.pageLoadTask.request->url.url;
        app.ShowNoHTTPSPage();
        assert(finished && title == "HTTPS unsupported");
        assert(address == expectedTarget && app.page.pageURL.url == expectedTarget);
        assert(network.destroyed == test + 1 && app.pageLoadTask.request == NULL);
        if (test == 1) assert(html.find("http://reader.test/?lg=en-us&amp;a=https%3A%2F%2Fexample.test%2Ffinal%3Fone%3D1%26two%3D2%23frag") != std::string::npos);
        else assert(html.find("<a href=") == std::string::npos);
        if (test == 3) assert(html.find("not been truncated or sent") != std::string::npos);
    }
    unsetenv("MICROWEB_READER");
    puts("PASS: production HTTPS error page captures redirect target before cleanup, keeps address bar, never auto-contacts reader and handles missing/invalid/oversized settings");
}
