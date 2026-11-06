#include <memory.h>
#include "Surf4pak.h"
#include "../Image/Image.h"
#include "../Platform.h"

DrawSurface_4BPP_Packed::DrawSurface_4BPP_Packed(int inWidth, int inHeight)
	: DrawSurface(inWidth, inHeight), cursorWidth(0), cursorHeight(0)
{
	lines = new uint8_t*[height];
	format = DrawSurface::Format_4BPP_Packed;
}

bool DrawSurface_4BPP_Packed::ClipRect(DrawContext& context, int& x, int& y, int& rectWidth, int& rectHeight)
{
	x += context.drawOffsetX;
	y += context.drawOffsetY;
	int left = context.clipLeft > 0 ? context.clipLeft : 0;
	int top = context.clipTop > 0 ? context.clipTop : 0;
	int right = context.clipRight < width ? context.clipRight : width;
	int bottom = context.clipBottom < height ? context.clipBottom : height;
	if (x < left) { rectWidth -= left - x; x = left; }
	if (y < top) { rectHeight -= top - y; y = top; }
	if (rectWidth > right - x) rectWidth = right - x;
	if (rectHeight > bottom - y) rectHeight = bottom - y;
	return rectWidth > 0 && rectHeight > 0;
}

void DrawSurface_4BPP_Packed::PutPixel(int x, int y, uint8_t colour)
{
	uint8_t* dest = lines[y] + (x >> 1);
	if (x & 1)
		*dest = (*dest & 0xf0) | (colour & 0x0f);
	else
		*dest = (*dest & 0x0f) | ((colour & 0x0f) << 4);
}

uint8_t DrawSurface_4BPP_Packed::GetPixel(int x, int y)
{
	uint8_t pixel = lines[y][x >> 1];
	return (x & 1) ? (pixel & 0x0f) : (pixel >> 4);
}

void DrawSurface_4BPP_Packed::Span(int x, int y, int count, uint8_t colour)
{
	if (count <= 0) return;
	colour &= 0x0f;
	uint8_t* dest = lines[y] + (x >> 1);
	if (x & 1)
	{
		*dest = (*dest & 0xf0) | colour;
		dest++;
		count--;
	}
	int bytes = count >> 1;
	memset(dest, colour | (colour << 4), bytes);
	if (count & 1)
	{
		dest += bytes;
		*dest = (*dest & 0x0f) | (colour << 4);
	}
}

void DrawSurface_4BPP_Packed::Clear()
{
	cursorHeight = 0;
	for (int y = 0; y < height; y++)
		Span(0, y, width, Platform::video->colourScheme.pageColour);
}

void DrawSurface_4BPP_Packed::HLine(DrawContext& context, int x, int y, int count, uint8_t colour)
{
	int h = 1;
	if (ClipRect(context, x, y, count, h)) Span(x, y, count, colour);
}

void DrawSurface_4BPP_Packed::VLine(DrawContext& context, int x, int y, int count, uint8_t colour)
{
	int w = 1;
	if (!ClipRect(context, x, y, w, count)) return;
	while (count--) PutPixel(x, y++, colour);
}

void DrawSurface_4BPP_Packed::FillRect(DrawContext& context, int x, int y, int rectWidth, int rectHeight, uint8_t colour)
{
	if (!ClipRect(context, x, y, rectWidth, rectHeight)) return;
	while (rectHeight--) Span(x, y++, rectWidth, colour);
}

void DrawSurface_4BPP_Packed::InvertRect(DrawContext& context, int x, int y, int rectWidth, int rectHeight)
{
	if (!ClipRect(context, x, y, rectWidth, rectHeight)) return;
	while (rectHeight--)
	{
		int count = rectWidth;
		uint8_t* dest = lines[y++] + (x >> 1);
		if (x & 1) { *dest++ ^= 0x0f; count--; }
		int bytes = count >> 1;
		while (bytes--) *dest++ ^= 0xff;
		if (count & 1) *dest ^= 0xf0;
	}
}

void DrawSurface_4BPP_Packed::DrawString(DrawContext& context, Font* font, const char* text, int x, int y, uint8_t colour, FontStyle::Type style)
{
	int startX = x;
	int bold = (style & FontStyle::Bold) ? 1 : 0;
	int left = context.clipLeft > 0 ? context.clipLeft : 0;
	int right = context.clipRight < width ? context.clipRight : width;
	int top = context.clipTop > 0 ? context.clipTop : 0;
	int bottom = context.clipBottom < height ? context.clipBottom : height;
	while (*text)
	{
		unsigned char c = (unsigned char)*text++;
		if (c < FIRST_FONT_GLYPH) continue;
		Font::Glyph& glyph = font->glyphs[c - FIRST_FONT_GLYPH];
		int glyphWidth = glyph.width;
		if (!glyphWidth) continue;
		int pitch = (glyphWidth + 7) >> 3;
		uint8_t* data = font->glyphData + glyph.offset;
		for (int row = glyph.top; row <= glyph.bottom; row++)
		{
			int outY = y + row + context.drawOffsetY;
			if (outY >= bottom) break;
			if (outY >= top)
			{
				int italic = (style & FontStyle::Italic) && row < (font->glyphHeight >> 1) ? 1 : 0;
				for (int col = 0; col < glyphWidth; col++)
				{
					if (data[col >> 3] & (0x80 >> (col & 7)))
					{
						int outX = x + col + italic + context.drawOffsetX;
						if (outX >= left && outX < right) PutPixel(outX, outY, colour);
						if (bold && outX + 1 >= left && outX + 1 < right) PutPixel(outX + 1, outY, colour);
					}
				}
			}
			data += pitch;
		}
		x += glyphWidth + bold;
		if (x + context.drawOffsetX >= context.clipRight) break;
	}
	if (style & FontStyle::Underline)
		HLine(context, startX, y + font->glyphHeight - 1, x - startX, colour);
}

void DrawSurface_4BPP_Packed::BlitImage(DrawContext& context, Image* image, int x, int y)
{
	if (!image->lines.IsAllocated() || (image->bpp != 8 && image->bpp != 1)) return;
	int sourceX = x + context.drawOffsetX;
	int sourceY = y + context.drawOffsetY;
	int destWidth = image->width;
	int destHeight = image->height;
	if (!ClipRect(context, x, y, destWidth, destHeight)) return;
	sourceX = x - sourceX;
	sourceY = y - sourceY;
	for (int row = 0; row < destHeight; row++)
	{
		// Reacquire the handles after each access: EMS/swap may remap them.
		MemBlockHandle* imageLines = image->lines.Get<MemBlockHandle*>();
		MemBlockHandle imageLine = imageLines[sourceY + row];
		uint8_t* source = imageLine.Get<uint8_t*>();
		for (int col = 0; col < destWidth; col++)
		{
			int sourceCol = sourceX + col;
			uint8_t pixel = image->bpp == 8 ? source[sourceCol]
				: ((source[sourceCol >> 3] & (0x80 >> (sourceCol & 7))) ? 15 : 0);
			if (pixel != TRANSPARENT_COLOUR_VALUE) PutPixel(x + col, y + row, pixel);
		}
	}
}

void DrawSurface_4BPP_Packed::VerticalScrollBar(DrawContext& context, int x, int y, int barHeight, int position, int size)
{
	// Same 16-pixel black/white widget used by the other colour surfaces.
	FillRect(context, x, y, 16, barHeight, 15);
	VLine(context, x, y, barHeight, 0);
	VLine(context, x + 15, y, barHeight, 0);
	if (position < 0) position = 0;
	if (size > barHeight - position) size = barHeight - position;
	if (size <= 0) return;
	HLine(context, x + 1, y + position + 1, 14, 0);
	HLine(context, x + 1, y + position + size - 2, 14, 0);
	VLine(context, x + 2, y + position + 2, size - 4, 0);
	VLine(context, x + 13, y + position + 2, size - 4, 0);
	if (size >= 11)
	{
		int middle = y + position + (size >> 1);
		HLine(context, x + 4, middle - 2, 8, 0);
		HLine(context, x + 4, middle, 8, 0);
		HLine(context, x + 4, middle + 2, 8, 0);
	}
}

void DrawSurface_4BPP_Packed::ScrollScreen(int top, int bottom, int scrollWidth, int amount)
{
	if (!amount || scrollWidth <= 0) return;
	if (top < 0) top = 0;
	if (bottom > height) bottom = height;
	if (top + amount < 0) top = -amount;
	if (bottom + amount > height) bottom = height - amount;
	if (scrollWidth > width) scrollWidth = width;
	if (top >= bottom) return;
	int bytes = scrollWidth >> 1;
	int row = amount > 0 ? top : bottom - 1;
	int end = amount > 0 ? bottom : top - 1;
	int step = amount > 0 ? 1 : -1;
	for (; row != end; row += step)
	{
		memmove(lines[row], lines[row + amount], bytes);
		if (scrollWidth & 1)
			lines[row][bytes] = (lines[row][bytes] & 0x0f) | (lines[row + amount][bytes] & 0xf0);
	}
}

void DrawSurface_4BPP_Packed::DrawCursor(MouseCursorData* cursor, int x, int y)
{
	HideCursor();
	if (!cursor) return;
	x -= cursor->hotSpotX;
	y -= cursor->hotSpotY;
	cursorX = x < 0 ? 0 : x;
	cursorY = y < 0 ? 0 : y;
	int right = x + 16 < width ? x + 16 : width;
	int bottom = y + 16 < height ? y + 16 : height;
	cursorWidth = right - cursorX;
	cursorHeight = bottom - cursorY;
	if (cursorWidth <= 0 || cursorHeight <= 0) { cursorHeight = 0; return; }
	for (int row = 0; row < cursorHeight; row++)
	{
		int screenY = cursorY + row;
		int cursorRow = screenY - y;
		for (int col = 0; col < cursorWidth; col++)
		{
			int screenX = cursorX + col;
			uint16_t mask = 0x8000u >> (screenX - x);
			uint8_t pixel = GetPixel(screenX, screenY);
			cursorBuffer[row * 16 + col] = pixel;
			if (!(cursor->data[cursorRow] & mask)) pixel = 0;
			if (cursor->data[cursorRow + 16] & mask) pixel ^= 15;
			PutPixel(screenX, screenY, pixel);
		}
	}
}

void DrawSurface_4BPP_Packed::HideCursor()
{
	for (int row = 0; row < cursorHeight; row++)
		for (int col = 0; col < cursorWidth; col++)
			PutPixel(cursorX + col, cursorY + row, cursorBuffer[row * 16 + col]);
	cursorHeight = 0;
}
