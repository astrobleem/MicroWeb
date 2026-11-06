#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "image_test_surface.h"
#include "../src/Image/Png.h"
#include "../src/Image/Image.h"
#include "../src/Platform.h"
#include "../src/VidModes.h"
#include "../src/Memory/Memory.h"

VideoDriver* Platform::video = NULL;
#ifdef PNG_REAL_ALLOCATOR
#include "../src/App.h"
#include <stdarg.h>
AppConfig App::config;
void Platform::FatalError(const char* message, ...)
{
    va_list args; va_start(args, message); vfprintf(stderr, message, args); va_end(args); abort();
}
#else
MemBlockAllocator MemoryManager::pageBlockAllocator;
static std::vector<void*> allocations;
static int failAllocation = -1;
MemBlockAllocator::MemBlockAllocator() {}
MemBlockHandle MemBlockAllocator::Allocate(uint16_t size)
{
    if (failAllocation == 0) return MemBlockHandle();
    if (failAllocation > 0) --failAllocation;
    void* p = malloc(size); assert(p); allocations.push_back(p); return MemBlockHandle(p);
}
void* MemBlockHandle::GetPtr() { assert(type == Conventional); return conventionalPointer; }
void MemBlockHandle::Commit() {}
#endif
class TestVideo : public VideoDriver
{
public:
    TestVideo(VideoModeInfo* mode) { videoMode = mode; }
    void Init(VideoModeInfo*) {} void Shutdown() {}
};
class TestPng : public PngDecoder
{
public:
    void Start(Image* image, int bpp, bool dimensions)
    {
        outputImage = image; onlyDownloadDimensions = dimensions; state = Decoding; image->bpp = bpp;
    }
};
int main(int argc, char** argv)
{
    assert(argc >= 3);
    FILE* file = fopen(argv[1], "rb"); assert(file);
    unsigned fragment = (unsigned)atoi(argv[2]); assert(fragment && fragment <= 65535);
    int bpp = argc > 3 ? atoi(argv[3]) : 8;
    bool dimensions = argc > 4 && atoi(argv[4]) != 0;
    #ifndef PNG_REAL_ALLOCATOR
    if (argc > 7) failAllocation = atoi(argv[7]);
    #endif
    VideoModeInfo mode; memset(&mode, 0, sizeof(mode)); mode.aspectRatio = mode.zoom = 100;
    TestVideo video(&mode); Platform::video = &video;
    uint8_t lut[256]; for (int n = 0; n < 256; ++n) lut[n] = (uint8_t)n;
    video.paletteLUT = lut; video.colourScheme.pageColour = 1;
    ImageTestSurface surface(DrawSurface::Format_4BPP_Packed);
    video.drawSurface = &surface;
    if (argc > 8 && !strcmp(argv[8], "tandy")) video.paletteLUT = egaPaletteLUT;
    Image image;
    if (argc > 6) { image.width = atoi(argv[5]); image.height = atoi(argv[6]); }
    TestPng* decoder = new TestPng(); decoder->Start(&image, bpp, dimensions);
    std::vector<uint8_t> buffer(fragment);
    size_t size;
    while ((size = fread(&buffer[0], 1, fragment, file)) != 0) decoder->Process(&buffer[0], size);
    fclose(file);
    printf("%d %u %u %d %u\n", decoder->GetState(), image.width, image.height, decoder->GetLinesDecoded(), (unsigned)sizeof(PngDecoder));
    if (decoder->GetState() == ImageDecoder::Success && !dimensions)
    {
        assert(image.lines.IsAllocated());
        MemBlockHandle* lines = image.lines.Get<MemBlockHandle*>();
        for (unsigned y = 0; y < image.height; ++y) fwrite(lines[y].GetPtr(), 1, image.pitch, stdout);
    }
    delete decoder;
    #ifndef PNG_REAL_ALLOCATOR
    for (size_t n = 0; n < allocations.size(); ++n) free(allocations[n]);
    #endif
    return 0;
}
