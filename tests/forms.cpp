#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/Node.h"
#include "../src/URL.h"
#define private public
#include "../src/Nodes/Form.h"
#undef private
#include "../src/FormEncoding.h"
#include "../src/Nodes/Field.h"
#include "../src/Nodes/CheckBox.h"
#include "../src/Nodes/Button.h"
#include "../src/Nodes/Select.h"

Node::Node(Type t) : styleHandle(0), type(t), isLayoutComplete(false) { anchor.x = anchor.y = size.x = size.y = 0; }

int main()
{
    const char* expected = "a%26%3D%2B%25%23%3F=x+y%2B%26%3D%25%23%3F%80";
    for (size_t cap = 0; cap < 100; ++cap)
    {
        char guarded[102]; memset(guarded, '@', sizeof(guarded));
        FormEncoding output(guarded + 1, cap);
        bool ok = output.Append("a&=+%#?", "x y+&=%#?\x80");
        assert(ok == (cap > strlen(expected)));
        assert(guarded[0] == '@' && guarded[cap + 1] == '@');
        if (ok) assert(!strcmp(guarded + 1, expected));
        else if (cap) assert(guarded[1] == 0);
    }
    char buffer[512];
    FormEncoding lines(buffer, sizeof(buffer));
    assert(lines.Append("line", "a\nb\rc\r\nd"));
    assert(!strcmp(buffer, "line=a%0D%0Ab%0D%0Ac%0D%0Ad"));
    char shortBuffer[8]; FormEncoding small(shortBuffer, sizeof(shortBuffer));
    assert(small.Append("x", "1"));
    assert(!small.Append("y", "%%%%"));
    assert(!strcmp(shortBuffer, "x=1"));
    assert(small.Append("z", "2")); assert(!strcmp(shortBuffer, "x=1&z=2"));
    assert(small.Append(NULL, "ignored") && small.Append("", "ignored"));

    FormNode::Data form;
    HiddenFieldData hidden("token", "a&b=+%"), nestedHidden("wrong", "nested");
    char text[] = "hello world"; char name[] = "q";
    TextFieldNode::Data field(text, sizeof(text), NULL); field.name = name;
    CheckBoxNode::Data checked("yes", NULL, false, true), unchecked("no", "bad", false, false);
    ButtonNode::Data first("First", NULL), second("Second", NULL);
    first.name = second.name = "submit"; first.value = "one"; second.value = "two";
    SelectNode::Data select("choice"); OptionNode::Data option;
    option.text = "Displayed label"; option.value = "actual+value"; select.selected = &option;
    FormNode::Data nested; nested.firstChild = &nestedHidden;
    form.firstChild = &hidden; hidden.next = &field; field.next = &checked; checked.next = &unchecked;
    unchecked.next = &first; first.next = &second; second.next = &select; select.next = &nested;
    FormEncoding output(buffer, sizeof(buffer));
    assert(FormNode::BuildAddressParameterList(&form, output, &second));
    assert(!strcmp(buffer, "token=a%26b%3D%2B%25&q=hello+world&yes=on&submit=two&choice=actual%2Bvalue"));
    // Input button/reset nodes deliberately have no submission metadata, even
    // if an event path incorrectly passes one as the clicked submitter.
    ButtonNode::Data inertButton("Visible button", NULL), inertReset("Reset", NULL);
    inertButton.next = &inertReset;
    FormEncoding inert(buffer, sizeof(buffer));
    assert(FormNode::BuildAddressParameterList(&inertButton, inert, &inertButton));
    assert(FormNode::BuildAddressParameterList(&inertReset, inert, &inertReset));
    assert(buffer[0] == 0);
    option.value = "";
    FormEncoding emptyOption(buffer, sizeof(buffer));
    assert(FormNode::BuildAddressParameterList(&form, emptyOption, NULL));
    assert(!strcmp(buffer, "token=a%26b%3D%2B%25&q=hello+world&yes=on&choice="));
    option.value = NULL;
    FormEncoding fallback(buffer, sizeof(buffer));
    assert(FormNode::BuildAddressParameterList(&select, fallback, NULL));
    assert(!strcmp(buffer, "choice=Displayed+label"));
    FormEncoding overflow(shortBuffer, sizeof(shortBuffer));
    assert(!FormNode::BuildAddressParameterList(&form, overflow, NULL));

    for (size_t n = 0; n < 520; ++n)
    {
        char value[520]; memset(value, 'x', n); value[n] = 0;
        FormEncoding boundary(buffer, sizeof(buffer));
        assert(boundary.Append("v", value) == (n <= 509));
    }
    char body[MAX_URL_LENGTH];
    URL address("http://example.test/search?old=1#anchor");
    assert(FormNode::BuildSubmission(&form, address, body, &first));
    assert(!strcmp(address.url, "http://example.test/search?token=a%26b%3D%2B%25&q=hello+world&yes=on&submit=one&choice=Displayed+label"));
    form.method = FormNode::Data::Post;
    address = "http://example.test/search?old=1#anchor";
    assert(FormNode::BuildSubmission(&form, address, body, &second));
    assert(!strcmp(address.url, "http://example.test/search?old=1"));
    assert(!strcmp(body, "token=a%26b%3D%2B%25&q=hello+world&yes=on&submit=two&choice=Displayed+label"));
    form.firstChild = NULL;
    assert(FormNode::BuildSubmission(&form, address, body, NULL));
    assert(body[0] == 0);
    form.method = FormNode::Data::Get;
    assert(FormNode::BuildSubmission(&form, address, body, NULL));
    assert(!strcmp(address.url, "http://example.test/search"));
    form.firstChild = &hidden;
    memset(address.url, 'a', 500); address.url[500] = 0;
    assert(!FormNode::BuildSubmission(&form, address, body, NULL));
    char huge[512]; memset(huge, '%', 511); huge[511] = 0;
    hidden.value = huge;
    form.method = FormNode::Data::Post;
    strcpy(body, "previous=request");
    address = "http://example.test/post";
    assert(!FormNode::BuildSubmission(&form, address, body, NULL));
    assert(!strcmp(body, "previous=request"));
    address = "http://example.test/dir/page?keep=1";
    assert(FormNode::ResolveAction(address, ""));
    assert(!strcmp(address.url, "http://example.test/dir/page?keep=1"));
    assert(FormNode::ResolveAction(address, "next"));
    assert(!strcmp(address.url, "http://example.test/dir/next"));
    assert(!FormNode::ResolveAction(address, huge));
    assert(FormNode::ResolveAction(address, "http://other.test/post"));
    assert(!strcmp(address.url, "http://other.test/post"));
    option.disabled = true;
    FormEncoding disabledOption(buffer, sizeof(buffer));
    assert(FormNode::BuildAddressParameterList(&select, disabledOption, NULL));
    assert(buffer[0] == 0);
    puts("PASS: form byte encoding, 100 capacities/canaries, CRLF, rollback, 512-byte limits, hidden fields, clicked submitter, checkbox defaults, option values/fallback, nested form isolation");
}
