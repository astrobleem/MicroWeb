#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "../src/ReaderLink.h"
#include "../src/URL.h"

struct Capture { std::string html; void Write(const char* s) { html += s; } };

int main()
{
    const char* prefix = "http://reader.test/read.php?lg=en-us&a=";
    const char* target = "https://example.test/a%20b?q=x&n=two+words#section";
    const char* expected = "http://reader.test/read.php?lg=en-us&a=https%3A%2F%2Fexample.test%2Fa%2520b%3Fq%3Dx%26n%3Dtwo%2Bwords%23section";
    char output[MAX_URL_LENGTH];
    for (size_t cap = 0; cap < MAX_URL_LENGTH; ++cap)
    {
        char guarded[MAX_URL_LENGTH + 2]; memset(guarded, '@', sizeof(guarded));
        ReaderLink::Result r = ReaderLink::Build(guarded + 1, cap, prefix, target);
        assert((r == ReaderLink::Ready) == (cap > strlen(expected)));
        assert(guarded[0] == '@' && guarded[cap + 1] == '@');
        if (r == ReaderLink::Ready) assert(!strcmp(guarded + 1, expected));
        else if (cap) assert(guarded[1] == 0);
    }
    assert(ReaderLink::Build(output, sizeof(output), NULL, target) == ReaderLink::NotConfigured);
    assert(ReaderLink::Build(output, sizeof(output), "", target) == ReaderLink::NotConfigured);
    assert(ReaderLink::Build(NULL, sizeof(output), prefix, target) == ReaderLink::TooLong);
    const char* invalid[] = { "https://reader.test/?a=", "ftp://reader.test/?a=", "http:///?a=", "http://?a=", "http://reader.test/", "http://reader.test/?", "http://reader.test/?=", "http://reader.test/?a=x", "http://reader.test/?a=x=", "http://reader.test/?a=&=", "http://reader.test/?a=#", "http://reader.test/?a='", "http://reader.test/?a=\"", "http://reader.test/?a= ", "http://reader.test/\n?a=", "http://user:password@reader.test/?a=", "http://reader.test\\evil/?a=" };
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i)
    {
        assert(ReaderLink::Build(output, sizeof(output), invalid[i], target) == ReaderLink::InvalidPrefix);
        assert(!output[0]);
    }
    const char* badTargets[] = { NULL, "", "https://", "http://example.test/", "https://example.test/\n", "https://example.test/\177" };
    for (size_t i = 0; i < sizeof(badTargets)/sizeof(badTargets[0]); ++i)
        assert(ReaderLink::Build(output, sizeof(output), prefix, badTargets[i]) == ReaderLink::InvalidTarget);
    assert(ReaderLink::Build(output, sizeof(output), "HTTP://reader.test:8080/?url=", "HTTPS://example.test/~a_b-1.2") == ReaderLink::Ready);
    assert(!strcmp(output, "HTTP://reader.test:8080/?url=HTTPS%3A%2F%2Fexample.test%2F~a_b-1.2"));
    assert(ReaderLink::Build(output, sizeof(output), prefix, "https://example.test/\"'><&\xC3\xA9") == ReaderLink::Ready);
    assert(strstr(output, "%22%27%3E%3C%26%C3%A9"));
    std::string longTarget = "https://example.test/";
    for (size_t i = 0; i < 600; ++i)
    {
        longTarget += i % 3 ? 'a' : '%';
        char guarded[MAX_URL_LENGTH + 2]; memset(guarded, '@', sizeof(guarded));
        ReaderLink::Result r = ReaderLink::Build(guarded + 1, MAX_URL_LENGTH, prefix, longTarget.c_str());
        assert(guarded[0] == '@' && guarded[MAX_URL_LENGTH + 1] == '@');
        if (r == ReaderLink::Ready) assert(strlen(guarded + 1) < MAX_URL_LENGTH);
        else assert(r == ReaderLink::TooLong && !guarded[1]);
    }
    assert(ReaderLink::Build(output, sizeof(output), prefix, target) == ReaderLink::Ready);
    Capture page; ReaderLink::WritePage(page, ReaderLink::Ready, output);
    assert(page.html.find("?lg=en-us&amp;a=https%3A") != std::string::npos);
    assert(page.html.find("unencrypted HTTP") != std::string::npos);
    assert(page.html.find("private links") != std::string::npos);
    assert(page.html.find("frogfind.com") == std::string::npos);
    for (int r = ReaderLink::NotConfigured; r <= ReaderLink::TooLong; ++r)
    {
        Capture error; ReaderLink::WritePage(error, (ReaderLink::Result)r, NULL);
        assert(error.html.find("<a href=") == std::string::npos);
        assert(error.html.find("MICROWEB_HTTPS_PROXY") != std::string::npos);
    }
    puts("PASS: reader encoding, all capacities, long inputs, prefix validation, HTML escaping, privacy notice and no default service");
}
