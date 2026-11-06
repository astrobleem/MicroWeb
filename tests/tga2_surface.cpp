// Host regression for TGA-II's linear 640x200, two-pixels-per-byte aperture.
// Link the production packed surface; only DOS video and memory handles are stubbed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/Draw/Surf4pak.h"
#include "../src/Image/Image.h"
#include "../src/Platform.h"

VideoDriver* Platform::video = NULL;
void* MemBlockHandle::GetPtr()
{
    if (type != Conventional) abort();
    return conventionalPointer;
}

static const int W = 640, H = 200, PITCH = 320;
static const int VISIBLE = PITCH * H, APERTURE = 65536, GUARD = 64;
static unsigned char ram[GUARD + APERTURE + GUARD];
static unsigned char reference[H][W];
static unsigned char before[H][W];
static DrawSurface_4BPP_Packed surface(W, H);
static const char* phase = "initialization";
static int testCase = 0;
static unsigned checks = 0;
static unsigned randomSeed = 0x54474132u;

static unsigned nextRandom()
{
    randomSeed = randomSeed * 1664525u + 1013904223u;
    return randomSeed;
}

static int randomRange(int low, int high)
{
    return low + nextRandom() % (high - low);
}

static void fail(const char* reason, int x, int y, int actual, int expected)
{
    fprintf(stderr, "FAIL: %s case %d: %s at (%d,%d): got %d, expected %d\n",
            phase, testCase, reason, x, y, actual, expected);
    abort();
}

static void checkGuards()
{
    for (int n = 0; n < GUARD; n++)
    {
        if (ram[n] != 0xa5) fail("leading aperture guard", n, -1, ram[n], 0xa5);
        if (ram[GUARD + APERTURE + n] != 0xa5)
            fail("trailing aperture guard", n, H, ram[GUARD + APERTURE + n], 0xa5);
    }
    // 640x200 at 4 bpp uses 64,000 bytes, not the entire 64 KiB aperture.
    for (int n = VISIBLE; n < APERTURE; n++)
        if (ram[GUARD + n] != 0xa5)
            fail("unused 1536-byte aperture tail", n, H, ram[GUARD + n], 0xa5);
}

static void check(MouseCursorData* cursor = NULL, int mouseX = 0, int mouseY = 0)
{
    checkGuards();
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
    {
        int expected = reference[y][x];
        if (cursor)
        {
            int cx = x - (mouseX - cursor->hotSpotX);
            int cy = y - (mouseY - cursor->hotSpotY);
            if (cx >= 0 && cx < 16 && cy >= 0 && cy < 16)
            {
                unsigned mask = 0x8000u >> cx;
                if (!(cursor->data[cy] & mask)) expected = 0;
                if (cursor->data[cy + 16] & mask) expected ^= 15;
            }
        }
        // Read by absolute aperture offset, independently of surface.lines.
        unsigned char packed = ram[GUARD + y * PITCH + x / 2];
        int actual = (x & 1) ? packed & 15 : packed >> 4;
        if (actual != expected) fail("pixel", x, y, actual, expected);
    }
    checks++;
}

static void setReference(const DrawContext& c, int x, int y, int colour, bool invert = false)
{
    x += c.drawOffsetX;
    y += c.drawOffsetY;
    if (x < 0 || x >= W || y < 0 || y >= H ||
        x < c.clipLeft || x >= c.clipRight || y < c.clipTop || y >= c.clipBottom) return;
    if (invert) reference[y][x] ^= 15;
    else reference[y][x] = colour & 15;
}

static void referenceRect(const DrawContext& c, int x, int y, int w, int h, int colour, bool invert = false)
{
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++)
        setReference(c, x + i, y + j, colour, invert);
}

static void resetPattern()
{
    surface.HideCursor();
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x += 2)
    {
        // Vary neighbouring rows and nibbles, including the far right/bottom.
        reference[y][x] = (x * 3 + y * 5 + (x >> 4) + (y >> 4)) & 15;
        reference[y][x + 1] = ((x + 1) * 3 + y * 5 + ((x + 1) >> 4) + (y >> 4)) & 15;
        ram[GUARD + y * PITCH + x / 2] = (reference[y][x] << 4) | reference[y][x + 1];
    }
}

static DrawContext contextFor(int variant)
{
    switch (variant)
    {
    case 0: return DrawContext(&surface, 0, 0, W, H);
    case 1: return DrawContext(&surface, 1, 1, W - 1, H - 1);
    case 2: return DrawContext(&surface, W - 19, H - 6, W, H);
    case 3: return DrawContext(&surface, -23, -17, W + 21, H + 11);
    case 4: return DrawContext(&surface, 5, 5, 5, H);
    default: return DrawContext(&surface, 20, 20, 10, 10);
    }
}

static void testPrimitives()
{
    DrawContext full = contextFor(0);
    phase = "full-aperture clear and palette";
    for (testCase = 0; testCase < 32; testCase++)
    {
        Platform::video->colourScheme.pageColour = testCase;
        surface.Clear();
        memset(reference, testCase & 15, sizeof(reference));
        check();
    }

    phase = "corner pixels and odd nibble endpoints";
    resetPattern();
    const int xs[] = {-1, 0, 1, 2, 317, 318, 319, 320, 321, W - 3, W - 2, W - 1, W};
    const int ys[] = {-1, 0, 1, H - 2, H - 1, H};
    testCase = 0;
    for (unsigned j = 0; j < sizeof(ys) / sizeof(ys[0]); j++)
        for (unsigned i = 0; i < sizeof(xs) / sizeof(xs[0]); i++)
            for (int count = 1; count <= 3; count++, testCase++)
            {
                surface.HLine(full, xs[i], ys[j], count, testCase & 15);
                referenceRect(full, xs[i], ys[j], count, 1, testCase & 15);
                check();
                surface.InvertRect(full, xs[i], ys[j], count, 1);
                referenceRect(full, xs[i], ys[j], count, 1, 0, true);
                check();
            }

    phase = "1000 randomized clipped primitives";
    for (testCase = 0; testCase < 1000; testCase++)
    {
        DrawContext c(&surface, randomRange(-20, W + 20), randomRange(-20, H + 20),
                      randomRange(-20, W + 20), randomRange(-20, H + 20));
        if (testCase % 3 == 0) c = contextFor(testCase % 6);
        c.drawOffsetX = randomRange(-20, 21);
        c.drawOffsetY = randomRange(-20, 21);
        int x = randomRange(-40, W + 40), y = randomRange(-40, H + 40);
        int w = randomRange(-3, W + 40), h = randomRange(-3, H + 40);
        int colour = randomRange(0, 256), operation = testCase % 4;
        if (operation == 0) { h = 1; surface.HLine(c, x, y, w, colour); }
        if (operation == 1) { w = 1; surface.VLine(c, x, y, h, colour); }
        if (operation == 2) surface.FillRect(c, x, y, w, h, colour);
        if (operation == 3) surface.InvertRect(c, x, y, w, h);
        referenceRect(c, x, y, w, h, colour, operation == 3);
        check();
    }

    phase = "oversized full-screen rectangle";
    surface.FillRect(full, -10, -10, W + 20, H + 20, 6);
    memset(reference, 6, sizeof(reference));
    check();
}

static void testScrolling()
{
    phase = "linear overlapping scroll and odd-width nibble preservation";
    const int regions[][2] = {{0, H}, {1, H - 1}, {7, H - 11}, {-13, H + 17},
                              {H - 2, H + 2}, {5, 5}, {H, 0}};
    const int amounts[] = {-250, -200, -199, -31, -7, -1, 0, 1, 7, 31, 199, 200, 250};
    const int widths[] = {-1, 0, 1, 2, 3, 17, 319, 320, 321, 637, 638, 639, 640, 641};
    testCase = 0;
    for (unsigned r = 0; r < sizeof(regions) / sizeof(regions[0]); r++)
        for (unsigned a = 0; a < sizeof(amounts) / sizeof(amounts[0]); a++)
            for (unsigned w = 0; w < sizeof(widths) / sizeof(widths[0]); w++, testCase++)
            {
                resetPattern();
                memcpy(before, reference, sizeof(before));
                int top = regions[r][0], bottom = regions[r][1], amount = amounts[a];
                surface.ScrollScreen(top, bottom, widths[w], amount);
                // Define expected copies by valid source/destination pixels,
                // without reproducing the implementation's row-order/clamp logic.
                if (amount) for (int y = 0; y < H; y++)
                    if (y >= top && y < bottom && y + amount >= 0 && y + amount < H)
                        for (int x = 0; x < W && x < widths[w]; x++)
                            reference[y][x] = before[y + amount][x];
                check();
            }
}

static void testImages()
{
    const int IW = 643, IH = 9;
    unsigned char* pixels[IH];
    MemBlockHandle handles[IH];
    for (int row = 0; row < IH; row++)
    {
        pixels[row] = new unsigned char[IW];
        for (int col = 0; col < IW; col++)
            pixels[row][col] = ((row * 7 + col) % 5 == 0) ? 255 : (row + col) & 15;
        handles[row] = MemBlockHandle(pixels[row]);
    }
    Image image;
    image.width = IW; image.height = IH; image.pitch = IW;
    image.bpp = 8; image.lines = MemBlockHandle(handles);
    const int xs[] = {-IW, -IW + 1, -19, -1, 0, 1, 319, 320, W - 17, W - 1, W};
    const int ys[] = {-IH, -IH + 1, -1, 0, 1, H - IH, H - 2, H - 1, H};
    phase = "transparent RGBI and monochrome images with four-sided clipping";
    testCase = 0;
    for (int bpp = 8; bpp >= 1; bpp -= 7)
    {
        image.bpp = bpp;
        image.pitch = bpp == 8 ? IW : (IW + 7) / 8;
        for (int variant = 0; variant < 6; variant++)
            for (unsigned j = 0; j < sizeof(ys) / sizeof(ys[0]); j++)
                for (unsigned i = 0; i < sizeof(xs) / sizeof(xs[0]); i++, testCase++)
                {
                    DrawContext c = contextFor(variant);
                    c.drawOffsetX = variant - 2;
                    c.drawOffsetY = 2 - variant;
                    resetPattern();
                    surface.BlitImage(c, &image, xs[i], ys[j]);
                    for (int row = 0; row < IH; row++) for (int col = 0; col < IW; col++)
                    {
                        int pixel = bpp == 8 ? pixels[row][col]
                            : ((pixels[row][col / 8] & (128 >> (col & 7))) ? 15 : 0);
                        if (pixel != 255) setReference(c, xs[i] + col, ys[j] + row, pixel);
                    }
                    check();
                }
    }
    phase = "empty and unsupported image no-ops";
    DrawContext full = contextFor(0);
    image.bpp = 4; surface.BlitImage(full, &image, 0, 0); check();
    image.bpp = 8; image.width = 0; surface.BlitImage(full, &image, 0, 0); check();
    image.width = IW; image.height = 0; surface.BlitImage(full, &image, 0, 0); check();
    image.height = IH; image.lines = MemBlockHandle();
    surface.BlitImage(full, &image, 0, 0); check();
    for (int row = 0; row < IH; row++) delete[] pixels[row];
}

static void testStyledText()
{
    phase = "styled multi-byte text with right/bottom and offset clipping";
    unsigned char fontStorage[sizeof(Font) + 12];
    memset(fontStorage, 0, sizeof(fontStorage));
    Font* font = reinterpret_cast<Font*>(fontStorage);
    font->glyphHeight = 6;
    Font::Glyph& glyph = font->glyphs['A' - FIRST_FONT_GLYPH];
    glyph.width = 9; glyph.top = 1; glyph.bottom = 4;
    for (int n = 0; n < 8; n++) font->glyphData[n] = n % 2 ? 0x80 : 0xa5;
    const int xs[] = {-20, -9, -1, 0, 1, 319, W - 20, W - 10, W - 2, W - 1, W};
    const int ys[] = {-6, -3, 0, 1, H - 6, H - 3, H - 1, H};
    testCase = 0;
    for (int style = 0; style < 8; style++)
        for (unsigned j = 0; j < sizeof(ys) / sizeof(ys[0]); j++)
            for (unsigned i = 0; i < sizeof(xs) / sizeof(xs[0]); i++, testCase++)
            {
                DrawContext c = contextFor(testCase % 4);
                c.drawOffsetX = (testCase % 3) - 1;
                c.drawOffsetY = 1 - (testCase % 3);
                resetPattern();
                surface.DrawString(c, font, "AA", xs[i], ys[j], 3, (FontStyle::Type)style);
                int pen = xs[i];
                for (int letter = 0; letter < 2; letter++)
                {
                    for (int row = 1; row <= 4; row++) for (int col = 0; col < 9; col++)
                        if (font->glyphData[(row - 1) * 2 + col / 8] & (128 >> (col & 7)))
                        {
                            int italic = (style & FontStyle::Italic) && row < 3 ? 1 : 0;
                            setReference(c, pen + col + italic, ys[j] + row, 3);
                            if (style & FontStyle::Bold)
                                setReference(c, pen + col + italic + 1, ys[j] + row, 3);
                        }
                    pen += 9 + ((style & FontStyle::Bold) ? 1 : 0);
                    if (pen + c.drawOffsetX >= c.clipRight) break;
                }
                if (style & FontStyle::Underline)
                    for (int col = xs[i]; col < pen; col++) setReference(c, col, ys[j] + 5, 3);
                check();
            }
}

static void testCursor()
{
    phase = "cursor edges, overlapping moves, null cursor and repeated restoration";
    MouseCursorData cursor;
    memset(&cursor, 0, sizeof(cursor));
    cursor.hotSpotX = 7; cursor.hotSpotY = 8;
    for (int row = 0; row < 16; row++)
    {
        cursor.data[row] = row & 1 ? 0xa5a5 : 0x5a5a;
        cursor.data[row + 16] = row & 2 ? 0x5aa5 : 0xa55a;
    }
    const int xs[] = {-17, -1, 0, 1, 7, 8, 15, W / 2, W - 16, W - 9, W - 8, W - 2, W - 1, W, W + 7, W + 16};
    const int ys[] = {-17, -1, 0, 1, 7, 8, 15, H / 2, H - 16, H - 9, H - 8, H - 2, H - 1, H, H + 8, H + 16};
    testCase = 0;
    for (unsigned j = 0; j < sizeof(ys) / sizeof(ys[0]); j++)
        for (unsigned i = 0; i < sizeof(xs) / sizeof(xs[0]); i++, testCase++)
        {
            resetPattern();
            surface.DrawCursor(&cursor, xs[i], ys[j]);
            check(&cursor, xs[i], ys[j]);
            // Drawing another cursor must restore the old one first, even
            // when the new cursor overlaps it or lies wholly offscreen.
            surface.DrawCursor(&cursor, xs[i] + 3, ys[j] - 2);
            check(&cursor, xs[i] + 3, ys[j] - 2);
            surface.HideCursor(); surface.HideCursor(); check();
            surface.DrawCursor(&cursor, xs[i], ys[j]);
            surface.DrawCursor(NULL, 0, 0); check();
        }
    phase = "clear invalidates saved cursor pixels";
    surface.DrawCursor(&cursor, W - 1, H - 1);
    Platform::video->colourScheme.pageColour = 4;
    surface.Clear(); memset(reference, 4, sizeof(reference));
    surface.HideCursor(); check();
}

class TestVideo : public VideoDriver
{
public:
    void Init(VideoModeInfo*) {}
    void Shutdown() {}
};

int main()
{
    memset(ram, 0xa5, sizeof(ram));
    for (int y = 0; y < H; y++) surface.lines[y] = ram + GUARD + y * PITCH;
    TestVideo video;
    Platform::video = &video;
    if (surface.width != W || surface.height != H || surface.format != DrawSurface::Format_4BPP_Packed)
        fail("surface metadata", 0, 0, surface.format, DrawSurface::Format_4BPP_Packed);
    testPrimitives();
    testScrolling();
    testImages();
    testStyledText();
    testCursor();
    delete[] surface.lines;
    printf("PASS: TGA-II 640x200 linear packed surface; %u complete framebuffer/guard checks; "
           "64,000 visible bytes and 1,536-byte unused tail; clipped primitives/images/styled text; "
           "bidirectional odd-width scrolling; cursor edge/move/restore\n", checks);
    return 0;
}
