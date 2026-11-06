#pragma once
#ifndef _FORM_H_
#define _FORM_H_


#include <stdint.h>
#include "../Node.h"

class FormEncoding;
struct URL;

class HiddenFieldData : public Node
{
public:
    HiddenFieldData(const char* inName, const char* inValue) : Node(Node::HiddenField), name(inName), value(inValue) {}
    const char* name;
    const char* value;
};

class FormNode : public NodeHandler
{
public:
	class Data : public Node
	{
	public:
		enum MethodType
		{
			Get,
			Post
		};
		char* action;
		MethodType method;

		Data() : Node(Node::Form), action(NULL), method(Get) {}
	};

	static FormNode::Data* Construct(Allocator& allocator);

	static void SubmitForm(Node* node, Node* submitter = NULL);

	static void OnSubmitButtonPressed(Node* node);

private:
    static bool ResolveAction(URL& address, const char* action);
    static bool BuildSubmission(Node* node, URL& address, char* postBody, Node* submitter);
    static bool BuildAddressParameterList(Node* node, FormEncoding& output, Node* submitter);
};

#endif
