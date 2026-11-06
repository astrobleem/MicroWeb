#ifndef _BUTTON_H_
#define _BUTTON_H_

#include "../Node.h"

class ButtonNode : public NodeHandler
{
public:
	class Data : public Node
	{
	public:
		Data(const char* inButtonText, NodeCallbackFunction inOnClick) : Node(Node::Button), buttonText(inButtonText), onClick(inOnClick), name(NULL), value(NULL) {}
		const char* buttonText;
		NodeCallbackFunction onClick;
		const char* name;
		const char* value;
	};

	ButtonNode() : focusingFromMouseClick(false) {}

	static ButtonNode::Data* Construct(Allocator& allocator, const char* buttonText, NodeCallbackFunction onClick);
	virtual void GenerateLayout(Layout& layout, Node* node) override;
	virtual void Draw(DrawContext& context, Node* node) override;
	virtual bool CanPick(Node* node) { return true; }
	virtual bool HandleEvent(Node* node, const Event& event);

	static Coord CalculateSize(Node* node);
	void InvertButton(Node* node);
	void HighlightButton(Node* node, uint8_t colour);

	bool focusingFromMouseClick;
};

#endif
