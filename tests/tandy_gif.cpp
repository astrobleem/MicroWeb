#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include "../src/Image/Gif.h"
#include "../src/Image/Image.h"
#include "../src/Draw/Surface.h"
#include "../src/Memory/Memory.h"
#include "../src/Platform.h"

// Host substitutes for allocation and decoder setup. Gif.cpp and the actual
// video lookup tables are compiled unchanged into this executable.
static std::vector<void*> allocations;
VideoDriver* Platform::video = NULL;
MemBlockAllocator MemoryManager::pageBlockAllocator;
MemBlockAllocator::MemBlockAllocator() {}
MemBlockHandle MemBlockAllocator::Allocate(uint16_t size)
{
    void* p = malloc(size);
    assert(p);
    allocations.push_back(p);
    return MemBlockHandle(p);
}
void* MemBlockHandle::GetPtr() { assert(type == Conventional); return conventionalPointer; }
void MemBlockHandle::Commit() { assert(type == Conventional); }
void Platform::FatalError(const char* message, ...)
{
    va_list args;
    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);
    abort();
}

// These streaming helpers match Decoder.cpp. Decoder.cpp also contains the
// DOS/application decoder factory, which is intentionally outside this test.
bool ImageDecoder::FillStruct(uint8_t** data, size_t& length, void* out, size_t size)
{
    const size_t count = std::min(size - structFillPosition, length);
    memcpy((uint8_t*)out + structFillPosition, *data, count);
    *data += count;
    length -= count;
    structFillPosition += count;
    if (structFillPosition != size) return false;
    structFillPosition = 0;
    return true;
}
bool ImageDecoder::SkipBytes(uint8_t** data, size_t& length, size_t size)
{
    const size_t count = std::min(size - structFillPosition, length);
    *data += count;
    length -= count;
    structFillPosition += count;
    if (structFillPosition != size) return false;
    structFillPosition = 0;
    return true;
}
void ImageDecoder::Begin(Image* image, bool dimensionsOnly)
{
    onlyDownloadDimensions = dimensionsOnly;
    state = Decoding;
    outputImage = image;
    outputImage->bpp = Platform::video->drawSurface->format == DrawSurface::Format_1BPP ? 1 : 8;
}
void ImageDecoder::CalculateImageDimensions(int width, int height)
{
    outputImage->sourceWidth = width;
    outputImage->sourceHeight = height;
    // Tests specify both output dimensions or neither; no mode aspect/zoom is
    // needed to exercise the production GIF emitter's scaling paths.
    assert((outputImage->width == 0) == (outputImage->height == 0));
    if (!outputImage->width) outputImage->width = width;
    if (!outputImage->height) outputImage->height = height;
}
const int8_t ImageDecoder::colourDitherMatrix[16] =
{
    -32, 0, -24, 8, 16, -16, 24, -8,
    -20, 12, -28, 4, 28, -4, 20, -12
};
const uint8_t ImageDecoder::greyDitherMatrix[256] =
{
    0,128,32,160,8,136,40,168,2,130,34,162,10,138,42,170,
    192,64,224,96,200,72,232,104,194,66,226,98,202,74,234,106,
    48,176,16,144,56,184,24,152,50,178,18,146,58,186,26,154,
    240,112,208,80,248,120,216,88,242,114,210,82,250,122,218,90,
    12,140,44,172,4,132,36,164,14,142,46,174,6,134,38,166,
    204,76,236,108,196,68,228,100,206,78,238,110,198,70,230,102,
    60,188,28,156,52,180,20,148,62,190,30,158,54,182,22,150,
    252,124,220,92,244,116,212,84,254,126,222,94,246,118,214,86,
    3,131,35,163,11,139,43,171,1,129,33,161,9,137,41,169,
    195,67,227,99,203,75,235,107,193,65,225,97,201,73,233,105,
    51,179,19,147,59,187,27,155,49,177,17,145,57,185,25,153,
    243,115,211,83,251,123,219,91,241,113,209,81,249,121,217,89,
    15,143,47,175,7,135,39,167,13,141,45,173,5,133,37,165,
    207,79,239,111,199,71,231,103,205,77,237,109,197,69,229,101,
    63,191,31,159,55,183,23,151,61,189,29,157,53,181,21,149,
    254,127,223,95,247,119,215,87,253,125,221,93,245,117,213,85
};

class TestSurface : public DrawSurface
{
public:
    TestSurface(Format f) : DrawSurface(320, 200) { format = f; lines = NULL; }
    void Clear() {}
    void HLine(DrawContext&, int, int, int, uint8_t) {}
    void VLine(DrawContext&, int, int, int, uint8_t) {}
    void FillRect(DrawContext&, int, int, int, int, uint8_t) {}
    void InvertRect(DrawContext&, int, int, int, int) {}
    void DrawString(DrawContext&, Font*, const char*, int, int, uint8_t, FontStyle::Type) {}
    void BlitImage(DrawContext&, Image*, int, int) {}
    void VerticalScrollBar(DrawContext&, int, int, int, int, int) {}
};
class TestVideo : public VideoDriver
{
public:
    TestVideo(DrawSurface* surface) { drawSurface = surface; paletteLUT = egaPaletteLUT; }
    void Init(VideoModeInfo*) {}
    void Shutdown() {}
};
struct RGB { uint8_t r, g, b; };
static const RGB rgbi[16] =
{
    {0,0,0}, {0,0,170}, {0,170,0}, {0,170,170},
    {170,0,0}, {170,0,170}, {170,85,0}, {170,170,170},
    {85,85,85}, {85,85,255}, {85,255,85}, {85,255,255},
    {255,85,85}, {255,85,255}, {255,255,85}, {255,255,255}
};
static std::vector<RGB> nativePalette(int count = 16, bool shuffled = true)
{
    std::vector<RGB> result;
    for (int i = 0; i < count; ++i) result.push_back(rgbi[shuffled ? (i * 5 + 3) % 16 : i % 16]);
    return result;
}
static std::vector<RGB> ordinaryPalette()
{
    std::vector<RGB> result;
    for (int i = 0; i < 16; ++i)
    {
        RGB rgb = {uint8_t(17 + i * 11), uint8_t(243 - i * 13), uint8_t(9 + i * 15)};
        result.push_back(rgb);
    }
    return result;
}
struct Fixture
{
    const char* name;
    const char* version;
    int width, height, transparent;
    bool interlaced;
    std::vector<RGB> global, local;
    std::vector<uint8_t> pixels;
    Fixture(const char* label) : name(label), version("GIF89a"), width(64), height(8),
        transparent(-1), interlaced(false), global(nativePalette()) { fillPixels(); }
    void fillPixels()
    {
        pixels.resize(width * height);
        // Every palette index appears at every position in the 4x4 dither.
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) pixels[y * width + x] = (x / 4 + y / 4) % 16;
    }
    const std::vector<RGB>& palette() const { return local.empty() ? global : local; }
};
static void word(std::vector<uint8_t>& bytes, int value)
{
    bytes.push_back(value & 255);
    bytes.push_back((value >> 8) & 255);
}
static int tableBits(const std::vector<RGB>& palette)
{
    int bits = 1;
    while ((1u << bits) < palette.size()) ++bits;
    assert(palette.size() == (1u << bits) && bits <= 8);
    return bits;
}
static void table(std::vector<uint8_t>& bytes, const std::vector<RGB>& palette)
{
    for (size_t i = 0; i < palette.size(); ++i)
    {
        bytes.push_back(palette[i].r);
        bytes.push_back(palette[i].g);
        bytes.push_back(palette[i].b);
    }
}
static void code(std::vector<uint8_t>& bytes, unsigned& bit, int value, int width)
{
    for (int i = 0; i < width; ++i, ++bit)
    {
        if (!(bit & 7)) bytes.push_back(0);
        bytes.back() |= ((value >> i) & 1) << (bit & 7);
    }
}
static std::vector<uint8_t> makeGif(const Fixture& f, size_t subBlockSize)
{
    assert(subBlockSize > 0 && subBlockSize <= 255);
    std::vector<uint8_t> bytes(f.version, f.version + 6);
    word(bytes, f.width); word(bytes, f.height);
    bytes.push_back(f.global.empty() ? 0 : 0x80 | (tableBits(f.global) - 1));
    bytes.push_back(0); bytes.push_back(0);
    table(bytes, f.global);
    if (f.transparent >= 0)
    {
        assert(!strcmp(f.version, "GIF89a"));
        bytes.push_back(0x21); bytes.push_back(0xf9); bytes.push_back(4);
        bytes.push_back(1); word(bytes, 0); bytes.push_back(f.transparent); bytes.push_back(0);
    }
    bytes.push_back(0x2c);
    word(bytes, 0); word(bytes, 0); word(bytes, f.width); word(bytes, f.height);
    bytes.push_back((f.local.empty() ? 0 : 0x80 | (tableBits(f.local) - 1)) | (f.interlaced ? 0x40 : 0));
    table(bytes, f.local);
    const int minCodeSize = std::max(2, tableBits(f.palette()));
    bytes.push_back(minCodeSize);
    std::vector<uint8_t> packed;
    unsigned bit = 0;
    const int clear = 1 << minCodeSize;
    // A clear code before each literal is legal GIF LZW, keeps the code width
    // fixed, and isolates palette/streaming regressions from a test compressor.
    std::vector<int> rows;
    if (f.interlaced)
    {
        const int first[] = {0,4,2,1}, step[] = {8,8,4,2};
        for (int pass = 0; pass < 4; ++pass)
            for (int y = first[pass]; y < f.height; y += step[pass]) rows.push_back(y);
    }
    else for (int y = 0; y < f.height; ++y) rows.push_back(y);
    for (size_t row = 0; row < rows.size(); ++row)
        for (int x = 0; x < f.width; ++x)
        {
            const int pixel = f.pixels[rows[row] * f.width + x];
            assert(pixel < (int)f.palette().size());
            code(packed, bit, clear, minCodeSize + 1);
            code(packed, bit, pixel, minCodeSize + 1);
        }
    code(packed, bit, clear + 1, minCodeSize + 1);
    for (size_t offset = 0; offset < packed.size(); offset += subBlockSize)
    {
        const size_t count = std::min(subBlockSize, packed.size() - offset);
        bytes.push_back(count);
        bytes.insert(bytes.end(), packed.begin() + offset, packed.begin() + offset + count);
    }
    bytes.push_back(0); bytes.push_back(0x3b);
    return bytes;
}
static int nativeIndex(RGB rgb)
{
    for (int i = 0; i < 16; ++i)
        if (rgb.r == rgbi[i].r && rgb.g == rgbi[i].g && rgb.b == rgbi[i].b) return i;
    return -1;
}
static int clamp(int value) { return std::max(0, std::min(255, value)); }
static int expectedDither(RGB rgb, int x, int y)
{
    const int bayer[4][4] = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    const int offset = bayer[y % 4][x % 4] * 4 - 32;
    const int r = clamp(rgb.r + offset), g = clamp(rgb.g + offset), b = clamp(rgb.b + offset);
    return egaPaletteLUT[(r / 32) * 32 + (g / 32) * 4 + b / 64];
}
static int greyThreshold(int x, int y)
{
    const int bayer2[2][2] = {{0,2},{3,1}};
    int value = 0;
    for (int bit = 0; bit < 4; ++bit)
        value += bayer2[(y >> bit) & 1][(x >> bit) & 1] << (6 - 2 * bit);
    // Preserve the existing 16x16 table, including its final-row first value.
    return (y % 16 == 15 && x % 16 == 0) ? 254 : value;
}
static unsigned runs = 0, checkedPixels = 0;
static void verify(const Fixture& f, bool direct, DrawSurface::Format format,
                   int outWidth, int outHeight, size_t chunk, size_t subBlockSize)
{
    assert(allocations.empty());
    TestSurface surface(format);
    TestVideo video(&surface);
    Platform::video = &video;
    Image image;
    image.width = outWidth;
    image.height = outHeight;
    GifDecoder decoder;
    decoder.Begin(&image, false);
    std::vector<uint8_t> bytes = makeGif(f, subBlockSize);
    decoder.Process(&bytes[0], 0);
    for (size_t pos = 0; pos < bytes.size(); pos += chunk)
    {
        decoder.Process(&bytes[pos], std::min(chunk, bytes.size() - pos));
        decoder.Process(&bytes[pos], 0);
        assert(decoder.GetState() != ImageDecoder::Error);
    }
    assert(decoder.GetState() == ImageDecoder::Success);
    assert(image.sourceWidth == f.width && image.sourceHeight == f.height);
    if (!outWidth) { outWidth = f.width; outHeight = f.height; }
    assert(image.width == outWidth && image.height == outHeight);
    const bool mono = format == DrawSurface::Format_1BPP;
    assert(image.bpp == (mono ? 1 : 8));
    assert(image.pitch == (mono ? (outWidth + 7) / 8 : outWidth));
    MemBlockHandle* lines = image.lines.Get<MemBlockHandle*>();
    int divider = 1;
    while (f.width / divider > GIF_LINE_BUFFER_MAX_SIZE) ++divider;
    assert(f.width % divider == 0);
    const int lineWidth = f.width / divider;
    unsigned differencesFromDither = 0;
    for (int y = 0; y < outHeight; ++y)
    {
        const int sourceY = ((y + 1) * f.height + outHeight - 1) / outHeight - 1;
        uint8_t* pixels = lines[y].Get<uint8_t*>();
        for (int x = 0; x < outWidth; ++x)
        {
            // Equivalent nearest-sample mapping, expressed without the
            // production emitter's incremental error accumulator.
            const int sampledX = (2 * x * lineWidth + outWidth - 1) / (2 * outWidth);
            const int sourceX = sampledX * divider + divider - 1;
            const int index = f.pixels[sourceY * f.width + sourceX];
            const RGB rgb = f.palette()[index];
            int expected = index == f.transparent ? 255 :
                direct ? nativeIndex(rgb) : expectedDither(rgb, x, y);
            if (mono)
            {
                const int grey = index == f.transparent ? 255 : (rgb.r * 76 + rgb.g * 150 + rgb.b * 30) / 256;
                expected = grey > greyThreshold(x, y);
            }
            assert(expected >= 0);
            const int actual = mono ? !!(pixels[x / 8] & (128 >> (x % 8))) : pixels[x];
            if (actual != expected)
            {
                fprintf(stderr, "%s: format=%d size=%dx%d chunk=%zu block=%zu at (%d,%d): got %u expected %d\n",
                        f.name, format, outWidth, outHeight, chunk, subBlockSize, x, y, actual, expected);
                abort();
            }
            if (index != f.transparent && expected != expectedDither(rgb, x, y)) ++differencesFromDither;
            ++checkedPixels;
        }
        if (mono && outWidth % 8) assert(!(pixels[outWidth / 8] & ((1 << (8 - outWidth % 8)) - 1)));
    }
    // Native fixtures cover colors for which the existing dither path visibly
    // differs, so these cannot pass accidentally by testing invariant colors.
    if (direct && outWidth == f.width && f.width >= 64) assert(differencesFromDither > 0);
    for (size_t i = 0; i < allocations.size(); ++i) free(allocations[i]);
    allocations.clear();
    Platform::video = NULL;
    ++runs;
}
static void exercise(const Fixture& f, bool direct, DrawSurface::Format format = DrawSurface::Format_4BPP_Packed)
{
    const size_t chunks[] = {1,2,3,7,19,512,65536};
    const size_t blocks[] = {1,7,255};
    for (size_t b = 0; b < sizeof(blocks) / sizeof(blocks[0]); ++b)
        for (size_t c = 0; c < sizeof(chunks) / sizeof(chunks[0]); ++c)
        {
            verify(f, direct, format, 0, 0, chunks[c], blocks[b]);
            verify(f, direct, format, 7, 3, chunks[c], blocks[b]);
            verify(f, direct, format, 79, 11, chunks[c], blocks[b]);
        }
}
int main()
{
    Fixture f("global GIF87a RGBI"); f.version = "GIF87a"; f.global = nativePalette(16, false);
    exercise(f, true);
    f = Fixture("global GIF89a reordered RGBI"); exercise(f, true);
    f = Fixture("local GIF89a RGBI without global table"); f.local = f.global; f.global.clear(); exercise(f, true);
    f = Fixture("local GIF87a overrides native global"); f.version = "GIF87a";
    f.local = nativePalette(16, false); exercise(f, true);
    f = Fixture("local RGBI overrides ordinary global"); f.local = f.global; f.global = ordinaryPalette(); exercise(f, true);
    f = Fixture("global transparent RGBI"); f.transparent = 7; exercise(f, true);
    f = Fixture("global non-native transparent entry"); f.transparent = 8;
    f.global[8] = RGB{1,2,3}; exercise(f, true);
    f = Fixture("local non-native transparent entry"); f.transparent = 5; f.local = f.global;
    f.local[5] = RGB{91,37,201}; exercise(f, true);
    f = Fixture("256-entry native table and transparent index 255"); f.local = nativePalette(256);
    f.global.clear(); f.transparent = 255; f.local[255] = RGB{17,18,19};
    for (size_t i = 0; i < f.pixels.size(); ++i) f.pixels[i] = (i % 7 == 0) ? 255 : f.pixels[i] + 128;
    exercise(f, true);
    f = Fixture("one nearly-native global color requires dithering"); f.global[8].r ^= 1; exercise(f, false);
    f = Fixture("one ordinary local color disables native global shortcut"); f.local = f.global;
    f.local[11] = RGB{100,120,140}; exercise(f, false);
    f = Fixture("ordinary global GIF retains ordered dithering"); f.global = ordinaryPalette(); exercise(f, false);
    f = Fixture("ordinary local GIF retains ordered dithering and transparency"); f.local = ordinaryPalette();
    f.transparent = 3; exercise(f, false);
    f = Fixture("interlaced native GIF"); f.interlaced = true; exercise(f, true);
    f = Fixture("large native GIF line-buffer downsampling"); f.width = 1280; f.height = 8; f.fillPixels();
    verify(f, true, DrawSurface::Format_4BPP_Packed, 320, 2, 1, 7);
    verify(f, true, DrawSurface::Format_4BPP_Packed, 320, 2, 257, 255);
    // Same video lookup table as packed Tandy, different surface format. The
    // shortcut must not silently change EGA, PC1512, CGA, VGA or VESA GIFs.
    const DrawSurface::Format formats[] = {DrawSurface::Format_4BPP_EGA, DrawSurface::Format_4BPP_PC1512,
        DrawSurface::Format_2BPP, DrawSurface::Format_8BPP, DrawSurface::Format_8BPP_VESA};
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i)
    {
        f = Fixture("non-Tandy format keeps RGBI ordered dithering"); exercise(f, false, formats[i]);
        f.local = nativePalette(16, false); f.transparent = 8; exercise(f, false, formats[i]);
    }
    f = Fixture("monochrome native global retains greyscale dithering");
    exercise(f, false, DrawSurface::Format_1BPP);
    f = Fixture("monochrome ordinary local retains greyscale dithering");
    f.local = ordinaryPalette(); f.transparent = 5;
    exercise(f, false, DrawSurface::Format_1BPP);
    printf("PASS: %u streamed GIF decodes; %u pixels; global/local RGBI, shuffled/256-entry palettes, "
           "transparency, resize/interlace, generic fallback, non-Tandy mode isolation\n", runs, checkedPixels);
    return 0;
}
