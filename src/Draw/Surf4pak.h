#ifndef _SURF4PAK_H_
#define _SURF4PAK_H_

#include "Surface.h"

// Two RGBI pixels per byte, with the left pixel in the high nibble.
// Scanline pointers hide the Tandy's four interleaved 8K video banks.
class DrawSurface_4BPP_Packed : public DrawSurface
{
public:
	DrawSurface_4BPP_Packed(int inWidth, int inHeight);

	virtual void Clear();
	virtual void HLine(DrawContext& context, int x, int y, int count, uint8_t colour);
	virtual void VLine(DrawContext& context, int x, int y, int count, uint8_t colour);
	virtual void FillRect(DrawContext& context, int x, int y, int width, int height, uint8_t colour);
	virtual void InvertRect(DrawContext& context, int x, int y, int width, int height);
	virtual void DrawString(DrawContext& context, Font* font, const char* text, int x, int y, uint8_t colour, FontStyle::Type style = FontStyle::Regular);
	virtual void BlitImage(DrawContext& context, Image* image, int x, int y);
	virtual void VerticalScrollBar(DrawContext& context, int x, int y, int height, int position, int size);
	virtual void ScrollScreen(int top, int bottom, int width, int amount);
	virtual void DrawCursor(struct MouseCursorData* cursor, int x, int y);
	virtual void HideCursor();

private:
	bool ClipRect(DrawContext& context, int& x, int& y, int& rectWidth, int& rectHeight);
	void Span(int x, int y, int count, uint8_t colour);
	void PutPixel(int x, int y, uint8_t colour);
	uint8_t GetPixel(int x, int y);

	int cursorX, cursorY, cursorWidth, cursorHeight;
	uint8_t cursorBuffer[16 * 16];
};

#endif
