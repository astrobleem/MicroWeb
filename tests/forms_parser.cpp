#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/Parser.h"
#include "../src/Tags.h"
#include "../src/Page.h"
#include "../src/Nodes/Form.h"
#include "../src/Nodes/Field.h"
#include "../src/Nodes/Button.h"
#include "../src/Nodes/Select.h"
#include "../src/Nodes/CheckBox.h"

// A tag-handler harness: actual production attribute parsing and tag handlers,
// with page emission captured instead of involving fonts/video/layout.
LinearAllocator MemoryManager::pageAllocator;
Node::Node(Type t) : styleHandle(0), type(t), isLayoutComplete(false) { anchor.x = anchor.y = size.x = size.y = 0; }
Node* Node::FindParentOfType(Node::Type t) { for (Node* p = parent.Get(); p; p = p->parent.Get()) if (p->type == t) return p; return NULL; }
Node* emitted;
Node* context;
HTMLParser::HTMLParser(Page& p) : page(p), contextStack(MemoryManager::pageAllocator) {}
void HTMLParser::EmitNode(Node* node) { if (node) { emitted = node; node->parent = context; } }
void HTMLParser::PushContext(Node* node, const HTMLTagHandler*) { EmitNode(node); context = node; }
void HTMLParser::PopContext(const HTMLTagHandler*) { context = context->parent.Get(); }
void FormNode::OnSubmitButtonPressed(Node*) {}
FormNode::Data* FormNode::Construct(Allocator& a) { return a.Alloc<FormNode::Data>(); }

int main()
{
    // Page is never accessed by these tag handlers or emission stubs.
    alignas(Page) unsigned char pageStorage[sizeof(Page)];
    HTMLParser parser(*reinterpret_cast<Page*>(pageStorage));
    InputTagHandler input("input"); ButtonTagHandler button;
    FormTagHandler form; SelectTagHandler select; OptionTagHandler option;
    char hidden[] = "type='hidden' name='t&amp;n' value='a&amp;b&#61;&quot;&apos;&#x2b;'";
    input.Open(parser, hidden);
    HiddenFieldData* h = static_cast<HiddenFieldData*>(emitted);
    assert(h->type == Node::HiddenField && !strcmp(h->name, "t&n") && !strcmp(h->value, "a&b=\"'+"));
    char disabled[] = "type='hidden' name='skip' value='secret' disabled";
    input.Open(parser, disabled); assert(!static_cast<HiddenFieldData*>(emitted)->name);
    char submit[] = "type='submit' name='go&amp;x' value='Send&amp;go'";
    input.Open(parser, submit);
    ButtonNode::Data* b = static_cast<ButtonNode::Data*>(emitted);
    assert(b->onClick && !strcmp(b->name, "go&x") && !strcmp(b->value, "Send&go"));
    const char* nonSubmitTypes[] = { "button", "reset", "BUTTON", "RESET" };
    for (unsigned int i = 0; i < sizeof(nonSubmitTypes) / sizeof(nonSubmitTypes[0]); ++i)
    {
        char attrs[100];
        sprintf(attrs, "type='%s' name='not_successful' value='Visible&amp;label'", nonSubmitTypes[i]);
        input.Open(parser, attrs);
        b = static_cast<ButtonNode::Data*>(emitted);
        assert(b->type == Node::Button && !b->onClick && !b->name && !b->value);
        assert(!strcmp(b->buttonText, "Visible&label"));
    }
    char resetDefault[] = "type='reset' name='reset_name'";
    input.Open(parser, resetDefault); b = static_cast<ButtonNode::Data*>(emitted);
    assert(b->type == Node::Button && !b->name && !b->onClick && !strcmp(b->buttonText, "Reset"));
    char omitted[] = "type='submit' name='default'";
    input.Open(parser, omitted); assert(!strcmp(static_cast<ButtonNode::Data*>(emitted)->buttonText, "Submit"));
    char buttonAttrs[] = "type='' name='go' value='x&amp;y'";
    button.Open(parser, buttonAttrs); b = static_cast<ButtonNode::Data*>(emitted);
    assert(b->onClick && !strcmp(b->value, "x&y")); button.Close(parser);
    char invalidType[] = "type='bogus'"; button.Open(parser, invalidType); assert(static_cast<ButtonNode::Data*>(emitted)->onClick); button.Close(parser);
    char plainButton[] = "type='button'"; button.Open(parser, plainButton); assert(!static_cast<ButtonNode::Data*>(emitted)->onClick); button.Close(parser);
    char disabledButton[] = "name='skip' disabled"; button.Open(parser, disabledButton); b = static_cast<ButtonNode::Data*>(emitted); assert(!b->onClick && !b->name); button.Close(parser);
    char formAttrs[] = "action='' method='post'"; form.Open(parser, formAttrs);
    assert(static_cast<FormNode::Data*>(emitted)->method == FormNode::Data::Post && !*static_cast<FormNode::Data*>(emitted)->action);
    char selectAttrs[] = "name='sel&amp;x'"; select.Open(parser, selectAttrs);
    SelectNode::Data* sel = static_cast<SelectNode::Data*>(emitted); assert(!strcmp(sel->name, "sel&x"));
    char optionAttrs[] = "value='a&amp;b' selected"; option.Open(parser, optionAttrs);
    OptionNode::Data* opt = static_cast<OptionNode::Data*>(emitted); assert(sel->selected == opt && !strcmp(opt->value, "a&b")); option.Close(parser);
    char emptyValue[] = "value='' disabled"; option.Open(parser, emptyValue); opt = static_cast<OptionNode::Data*>(emitted); assert(opt->value && !*opt->value && opt->disabled); option.Close(parser);
    char noValue[] = ""; option.Open(parser, noValue); assert(!static_cast<OptionNode::Data*>(emitted)->value); option.Close(parser); select.Close(parser);
    char unnamed[] = ""; select.Open(parser, unnamed); assert(!static_cast<SelectNode::Data*>(emitted)->name); select.Close(parser);
    puts("PASS: production tag/attribute parser wiring, entities, hidden/submit/button/select/option metadata, disabled fields, empty option values, unnamed selects");
}
