#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "../src/URL.h"

static void resolve(const char* base, const char* target, const char* expected)
{
    const char* actual = URL::GenerateFromRelative(base, target).url;
    if (strcmp(actual, expected)) { fprintf(stderr, "%s + %s -> %s (wanted %s)\n", base, target, actual, expected); assert(false); }
}
int main()
{
    resolve("file://index.htm", "link1.htm", "file://link1.htm");
    resolve("file://C:/index.htm", "link1.htm", "file://C:/link1.htm");
    resolve("file://C:/dir/index.htm", "../link1.htm", "file://C:/link1.htm");
    resolve("file://C:/dir/index.htm", "/link1.htm", "file://C:/link1.htm");
    resolve("file://dir/index.htm", "../link1.htm", "file://link1.htm");
    resolve("file://dir/index.htm", "../../link1.htm", "file://../link1.htm");
    resolve("file://C:\\dir\\index.htm", "..\\link1.htm", "file://C:/link1.htm");
    resolve("file://index.htm", "#a", "file://index.htm#a");
    resolve("http://host/a/index.htm", "?a=1&b=2", "http://host/a/index.htm?a=1&b=2");
    resolve("http://host/a/index.htm?q=old#old", "#new", "http://host/a/index.htm?q=old#new");
    resolve("http://host/a/index.htm?q=old#old", "", "http://host/a/index.htm?q=old");
    resolve("http://host/a/index.htm", "/p?q=/../x#y/./z", "http://host/p?q=/../x#y/./z");
    resolve("http://host/a/index.htm", "?a=\\value", "http://host/a/index.htm?a=\\value");
    resolve("http://host", "next", "http://host/next");
    resolve("http://host?q=old", "next", "http://host/next");
    resolve("http://host/a/index.htm", "../../../../next", "http://host/next");
    resolve("http://host/a/index.htm", "..", "http://host/");
    resolve("http://host/a/index.htm", ".", "http://host/a/");
    resolve("https://host/a/index.htm", "//other/p", "https://other/p");
    resolve("http://host/a/index.htm", "//other/p", "http://other/p");
    resolve("http://host/a/index.htm", "https://other/a/../p", "https://other/p");
    resolve("http://host/a/index.htm", "?a=1&amp;b=2", "http://host/a/index.htm?a=1&amp;b=2");
    resolve("http://host/a/index.htm", "?a=1&amp;amp;b=2", "http://host/a/index.htm?a=1&amp;amp;b=2");
    resolve("http://host/a/index.htm", "C:\\dir\\page.htm", "C:/dir/page.htm");
    resolve("http://host/a/index.htm", "http://other/a//b", "http://other/a//b");
    const URL& first = URL::GenerateFromRelative("http://host/a/page", "other");
    resolve(first.url, "next", "http://host/a/next");
    std::string huge(MAX_URL_LENGTH, 'x');
    assert(!URL(huge.c_str()).url[0]);
    assert(!URL::GenerateFromRelative("http://host/", huge.c_str()).url[0]);
    assert(!URL::GenerateFromRelative(huge.c_str(), "next").url[0]);
    std::string fits(MAX_URL_LENGTH - 1, 'x');
    URL bounded(fits.c_str()); assert(strlen(bounded.url) == MAX_URL_LENGTH - 1);
    assert(!URL::GenerateFromRelative("http://host/a/", fits.c_str()).url[0]);
    bounded = bounded.url; assert(strlen(bounded.url) == MAX_URL_LENGTH - 1);
    puts("PASS: local/HTTP/HTTPS URL resolution, opaque query/fragment, one-pass attribute boundary, aliasing and length limits");
}
