#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/Draw/Surf4pak.h"
#include "../src/Image/Image.h"
#include "../src/Platform.h"

VideoDriver* Platform::video = NULL;
void* MemBlockHandle::GetPtr() { assert(type == Conventional); return conventionalPointer; }

static const int W = 32, H = 20, GUARD = 16;
static unsigned char ram[4u * (8192u + GUARD * 2u)];
static unsigned char reference[H][W];
static DrawSurface_4BPP_Packed surface(W, H);
static unsigned randomSeed = 7;
static unsigned nextRandom() { randomSeed = randomSeed * 1664525u + 1013904223u; return randomSeed; }
static int randRange(int low, int high) { return low + nextRandom() % (high - low); }
static void setReference(DrawContext& c, int x, int y, int colour, bool invert = false)
{
 x += c.drawOffsetX; y += c.drawOffsetY;
 if(x < 0 || x >= W || y < 0 || y >= H || x < c.clipLeft || x >= c.clipRight || y < c.clipTop || y >= c.clipBottom) return;
 if(invert) reference[y][x] ^= 15; else reference[y][x] = colour & 15;
}
static void check()
{
 for(int bank = 0; bank < 4; bank++)
 {
  unsigned char* base = ram + bank * (8192 + GUARD * 2);
  for(int n = 0; n < GUARD; n++) assert(base[n] == 0xa5 && base[GUARD + 8192 + n] == 0xa5);
  for(int n = (H / 4) * (W / 2); n < 8192; n++) assert(base[GUARD + n] == 0xa5);
 }
 for(int y = 0; y < H; y++) for(int x = 0; x < W; x++)
 {
  int actual = (x & 1) ? surface.lines[y][x / 2] & 15 : surface.lines[y][x / 2] >> 4;
  assert(actual == reference[y][x]);
 }
}
class TestVideo : public VideoDriver { public: void Init(VideoModeInfo*) {} void Shutdown() {} };
int main()
{
 memset(ram, 0xa5, sizeof(ram));
 for(int y = 0; y < H; y++) surface.lines[y] = ram + (y & 3) * (8192 + GUARD * 2) + GUARD + (y >> 2) * (W / 2);
 TestVideo video; video.colourScheme.pageColour = 15; Platform::video = &video;
 DrawContext full(&surface, 0, 0, W, H);
 surface.Clear(); memset(reference, 15, sizeof(reference)); check();
 // Random rectangles and lines include odd nibble endpoints, empty regions,
 // offsets and clips extending beyond the physical surface.
 for(int n = 0; n < 10000; n++)
 {
  DrawContext c(&surface, randRange(-3, W), randRange(-3, H), randRange(0, W + 4), randRange(0, H + 4));
  c.drawOffsetX = randRange(-5, 6); c.drawOffsetY = randRange(-5, 6);
  int x = randRange(-10, W + 10), y = randRange(-10, H + 10), w = randRange(-2, W + 10), h = randRange(-2, H + 10), colour = randRange(0, 256), op = n % 4;
  if(op == 0) { h = 1; surface.HLine(c, x, y, w, colour); }
  if(op == 1) { w = 1; surface.VLine(c, x, y, h, colour); }
  if(op == 2) surface.FillRect(c, x, y, w, h, colour);
  if(op == 3) surface.InvertRect(c, x, y, w, h);
  for(int j = 0; j < h; j++) for(int i = 0; i < w; i++) setReference(c, x + i, y + j, colour, op == 3);
  check();
 }
 // Scrolling must preserve the unused nibble on odd-width copies, in either
 // direction, even though consecutive rows occupy different banks.
 for(int amount = -5; amount <= 5; amount++) for(int w = 1; w <= W; w++)
 {
  unsigned char old[H][W]; memcpy(old, reference, sizeof(old));
  int top = 5, bottom = H - 5;
  surface.ScrollScreen(top, bottom, w, amount);
  if(amount) for(int y = top; y < bottom; y++) for(int x = 0; x < w; x++) reference[y][x] = old[y + amount][x];
  check();
 }
 // Eight-bit image lines use EGA/RGBI indices; 255 is transparent.
 unsigned char pixels[3][9] = {{1,2,3,4,5,6,7,8,9},{15,255,0,255,12,13,14,15,1},{2,3,4,5,6,7,8,9,10}};
 MemBlockHandle handles[3]; for(int y = 0; y < 3; y++) handles[y] = MemBlockHandle(pixels[y]);
 Image image; image.width = 9; image.height = 3; image.pitch = 9; image.bpp = 8; image.lines = MemBlockHandle(handles);
 for(int x = -12; x < W + 3; x++)
 {
  DrawContext c(&surface, 3, 2, W - 1, H - 1); c.drawOffsetX = 2; c.drawOffsetY = 1;
  surface.BlitImage(c, &image, x, 4);
  for(int y = 0; y < 3; y++) for(int col = 0; col < 9; col++) if(pixels[y][col] != 255) setReference(c, x + col, 4 + y, pixels[y][col]);
  check();
 }
 image.bpp = 1;
 for(int x = -10; x < W + 2; x++)
 {
  surface.BlitImage(full, &image, x, 1);
  for(int y = 0; y < 3; y++) for(int col = 0; col < 9; col++) setReference(full, x + col, 1 + y, (pixels[y][col >> 3] & (128 >> (col & 7))) ? 15 : 0);
  check();
 }
 // Synthetic 9-pixel-wide glyph exercises multiple source bytes, trimming,
 // bold spill, italic, underline and clipping on all sides.
 unsigned char fontStorage[sizeof(Font) + 12]; memset(fontStorage, 0, sizeof(fontStorage));
 Font* font = (Font*)fontStorage; font->glyphHeight = 6;
 Font::Glyph& glyph = font->glyphs['A' - FIRST_FONT_GLYPH]; glyph.width = 9; glyph.top = 1; glyph.bottom = 4;
 for(int n = 0; n < 8; n++) font->glyphData[n] = n % 2 ? 0x80 : 0xa5;
 for(int style = 0; style < 8; style++) for(int x = -10; x < W + 2; x++)
 {
  DrawContext c(&surface, 2, 3, W - 2, H - 2); c.drawOffsetX = 1; c.drawOffsetY = 1;
  surface.DrawString(c, font, "AA", x, 1, 3, (FontStyle::Type)style);
  int pen = x;
  for(int letter = 0; letter < 2; letter++)
  {
   for(int row = 1; row <= 4; row++) for(int col = 0; col < 9; col++) if(font->glyphData[(row - 1) * 2 + (col >> 3)] & (128 >> (col & 7)))
   {
    int italic = (style & 2) && row < 3 ? 1 : 0;
    setReference(c, pen + col + italic, 1 + row, 3);
    if(style & 1) setReference(c, pen + col + italic + 1, 1 + row, 3);
   }
   pen += 9 + (style & 1); if(pen + c.drawOffsetX >= c.clipRight) break;
  }
  if(style & 4) for(int col = x; col < pen; col++) setReference(c, col, 6, 3);
  check();
 }
 // Software cursor follows INT 33h AND/XOR semantics and restores every pixel.
 MouseCursorData cursor; memset(&cursor, 0, sizeof(cursor)); cursor.hotSpotX = 7; cursor.hotSpotY = 8;
 for(int n = 0; n < 16; n++) { cursor.data[n] = 0x5a5a; cursor.data[n + 16] = 0xa55a; }
 for(int y = -10; y < H + 10; y++) for(int x = -10; x < W + 10; x++)
 {
  surface.DrawCursor(&cursor, x, y);
  for(int j = 0; j < H; j++) for(int i = 0; i < W; i++)
  {
   int expected = reference[j][i], cx = i - (x - 7), cy = j - (y - 8);
   if(cx >= 0 && cx < 16 && cy >= 0 && cy < 16)
   { if(!(cursor.data[cy] & (0x8000u >> cx))) expected = 0; if(cursor.data[cy + 16] & (0x8000u >> cx)) expected ^= 15; }
   int actual = (i & 1) ? surface.lines[j][i / 2] & 15 : surface.lines[j][i / 2] >> 4; assert(actual == expected);
  }
  surface.HideCursor(); surface.HideCursor(); check();
 }
 surface.VerticalScrollBar(full, W - 4, -5, H + 10, 1, 15);
 // Guard bytes still catch scrollbar writes outside the screen.
 for(int y = 0; y < H; y++) for(int x = 0; x < W; x++) reference[y][x] = (x & 1) ? surface.lines[y][x / 2] & 15 : surface.lines[y][x / 2] >> 4;
 check();
 delete[] surface.lines;
 puts("PASS: 10000 clipped primitives; banked/odd-width scrolling; RGBI/mono images; styled/clipped text; cursor edges/restore; scrollbar guards");
 return 0;
}
