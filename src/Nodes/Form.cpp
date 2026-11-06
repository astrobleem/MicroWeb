#include "Form.h"
#include "../Memory/Memory.h"
#include "../App.h"
#include "../Interface.h"
#include "Field.h"
#include "CheckBox.h"
#include "Select.h"
#include "Button.h"
#include "../FormEncoding.h"

#include "../HTTP.h"

FormNode::Data* FormNode::Construct(Allocator& allocator)
{
	return allocator.Alloc<FormNode::Data>();
}

bool FormNode::BuildAddressParameterList(Node* node, FormEncoding& output, Node* submitter)
{
    const char* name = NULL;
    const char* value = NULL;
    switch (node->type)
    {
    case Node::TextField:
        name = static_cast<TextFieldNode::Data*>(node)->name;
        value = static_cast<TextFieldNode::Data*>(node)->buffer;
        break;
    case Node::HiddenField:
        name = static_cast<HiddenFieldData*>(node)->name;
        value = static_cast<HiddenFieldData*>(node)->value;
        break;
    case Node::Button:
        if (node == submitter)
        {
            name = static_cast<ButtonNode::Data*>(node)->name;
            value = static_cast<ButtonNode::Data*>(node)->value;
        }
        break;
    case Node::CheckBox:
        if (static_cast<CheckBoxNode::Data*>(node)->isChecked)
        {
            name = static_cast<CheckBoxNode::Data*>(node)->name;
            value = static_cast<CheckBoxNode::Data*>(node)->value;
            if (!value) value = "on";
        }
        break;
    case Node::Select:
        {
            SelectNode::Data* select = static_cast<SelectNode::Data*>(node);
            if (select->selected && !select->selected->disabled)
            {
                name = select->name;
                value = select->selected->value ? select->selected->value : select->selected->text;
            }
        }
        break;
    }
    if (!output.Append(name, value)) return false;
    for (Node* child = node->firstChild.Get(); child; child = child->next.Get())
    {
        if (child->type != Node::Form && !BuildAddressParameterList(child, output, submitter)) return false;
    }
    return true;
}

bool FormNode::BuildSubmission(Node* node, URL& address, char* postBody, Node* submitter)
{
    FormNode::Data* data = static_cast<FormNode::Data*>(node);
    char* fragment = strchr(address.url, '#');
    if (fragment) *fragment = 0;
    if (data->method != Data::Post)
    {
        char* query = strchr(address.url, '?');
        if (query) *query = 0;
    }
    size_t length = strlen(address.url);
    if (data->method != Data::Post && length >= sizeof(address.url) - 1)
    {
        return false;
    }
    char* params = data->method == Data::Post ? postBody : address.url + length + 1;
    size_t capacity = data->method == Data::Post ? MAX_URL_LENGTH : sizeof(address.url) - length - 1;
    // Validate before touching the persistent POST body: a rejected oversized
    // submission must not corrupt an earlier request still being transmitted.
    if (data->method == Data::Post)
    {
        FormEncoding measured(NULL, capacity);
        if (!BuildAddressParameterList(node, measured, submitter)) return false;
    }
    FormEncoding output(params, capacity);
    if (!BuildAddressParameterList(node, output, submitter))
    {
        return false;
    }
    if (data->method != Data::Post && *params) address.url[length] = '?';
    return true;
}

bool FormNode::ResolveAction(URL& address, const char* action)
{
    if (!action || !*action) return true;
    // The legacy resolver concatenates without a capacity parameter. Reject
    // oversized input before calling it; allow its slash or fallback http:// prefix.
    size_t length = strlen(action);
    const char* protocol = strstr(action, "://");
    const char* query = strchr(action, '?');
    bool absolute = protocol && (!query || protocol < query);
    if (length >= MAX_URL_LENGTH || (!absolute &&
        strlen(address.url) + length + 8 >= MAX_URL_LENGTH)) return false;
    address = URL::GenerateFromRelative(address.url, action);
    return true;
}

void FormNode::SubmitForm(Node* node, Node* submitter)
{
	FormNode::Data* data = static_cast<FormNode::Data*>(node);
	App& app = App::Get();

	URL address(app.ui.addressBarURL);
	if (data->action && *data->action)
	{
		if (!strcmp(data->action, "download://"))
		{
			for (Node* child = node->firstChild.Get(); child; child = child->next.Get())
			{
				if(child->type == Node::TextField)
				{
					TextFieldNode::Data* fieldData = static_cast<TextFieldNode::Data*>(child);
					if (fieldData->name && !strcmp(fieldData->name, "path"))
					{
						App::Get().BeginFileDownload(fieldData->buffer);
					}
				}
			}
			return;
		}
		else if (!strcmp(data->action, "cancel://"))
		{
			App::Get().CancelFileDownload();
			return;
		}
        if (!ResolveAction(address, data->action))
        {
            app.ui.SetStatusMessage("Form action is too long", StatusBarNode::GeneralStatus);
            return;
        }
	}

    // The HTTP request retains its body pointer beyond this call. Keep a single
    // bounded body buffer (the browser only has one active page request).
    static char postBody[MAX_URL_LENGTH];
    if (!BuildSubmission(node, address, postBody, submitter))
    {
        app.ui.SetStatusMessage("Form is too large to submit", StatusBarNode::GeneralStatus);
        return;
    }
    if (data->method == Data::Post)
    {
        static HTTPOptions postOptions;
        postOptions.contentData = postBody;
        postOptions.keepAlive = false;
        postOptions.headerParams = NULL;
        postOptions.postContentType = "application/x-www-form-urlencoded";
        app.OpenURL(HTTPRequest::Post, address.url, &postOptions);
    }
    else
    {
        app.OpenURL(HTTPRequest::Get, address.url);
    }

}

void FormNode::OnSubmitButtonPressed(Node* node)
{
	Node* formNode = node->FindParentOfType(Node::Form);
	if (formNode)
	{
		SubmitForm(formNode, node->type == Node::Button ? node : NULL);
	}
}
