// Batched streaming harness. Compile this file against both decoder versions.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "Image/Jpeg.h"
#include "Image/Image.h"
#include "Platform.h"
#include "VidModes.h"
#include "Memory/Memory.h"

VideoDriver* Platform::video = NULL;
MemBlockAllocator MemoryManager::pageBlockAllocator;
static std::vector<void*> allocations;
MemBlockAllocator::MemBlockAllocator() {}
MemBlockHandle MemBlockAllocator::Allocate(uint16_t size)
{
    void* p = malloc(size);
    assert(p);
    allocations.push_back(p);
    return MemBlockHandle(p);
}
void* MemBlockHandle::GetPtr() { assert(type == Conventional); return conventionalPointer; }
void MemBlockHandle::Commit() {}

class TestVideo : public VideoDriver
{
public:
    TestVideo(VideoModeInfo* mode) { videoMode = mode; }
    void Init(VideoModeInfo*) {} void Shutdown() {}
};
class TestJpeg : public JpegDecoder
{
public:
    void Start(Image* image, int bpp)
    {
        outputImage = image;
        onlyDownloadDimensions = false;
        state = Decoding;
        image->bpp = bpp;
    }
};

int main(int argc, char** argv)
{
    assert(argc == 2);
    FILE* file = fopen(argv[1], "rb");
    assert(file);
    std::vector<uint8_t> data;
    int byte;
    while ((byte = fgetc(file)) != EOF) data.push_back((uint8_t)byte);
    assert(!ferror(file));
    fclose(file);

    VideoModeInfo mode;
    memset(&mode, 0, sizeof(mode));
    mode.aspectRatio = mode.zoom = 100;
    TestVideo video(&mode);
    Platform::video = &video;
    uint8_t lut[256];
    for (int n = 0; n < 256; ++n) lut[n] = (uint8_t)n;
    video.paletteLUT = lut;
    video.colourScheme.pageColour = 1;

    // One request per line: byte limit, chunk size, optional first split (-1
    // means regular chunks), output bpp, requested output width and height.
    unsigned limit, fragment, width, height, number = 0;
    int first, bpp;
    while (scanf("%u %u %d %d %u %u", &limit, &fragment, &first,
                 &bpp, &width, &height) == 6)
    {
        assert(limit <= data.size() && fragment && fragment <= 65535);
        assert(first >= -1 && (first < 0 || (unsigned)first <= limit));
        assert(bpp == 1 || bpp == 8);
        Image image;
        image.width = width;
        image.height = height;
        TestJpeg* decoder = new TestJpeg();
        decoder->Start(&image, bpp);
        unsigned offset = 0;
        if (first >= 0)
        {
            decoder->Process(first ? &data[0] : NULL, first);
            offset = first;
        }
        while (offset < limit)
        {
            unsigned count = limit - offset;
            if (count > fragment) count = fragment;
            decoder->Process(&data[offset], count);
            offset += count;
        }
        // Empty calls cannot make progress or change a completed/error state.
        // The parent puts a wall-clock timeout around the whole request batch.
        ImageDecoder::State state = decoder->GetState();
        int linesDecoded = decoder->GetLinesDecoded();
        for (int n = 0; n < 4; ++n) decoder->Process(NULL, 0);
        assert(state == decoder->GetState());
        assert(linesDecoded == decoder->GetLinesDecoded());
        unsigned pitch = bpp == 1 ? (image.width + 7) / 8 : image.width;
        unsigned bytes = state == ImageDecoder::Success ? pitch * image.height : 0;
        printf("%u %d %u %u %d %u %u\n", number++, state, image.width,
               image.height, linesDecoded, (unsigned)sizeof(JpegDecoder), bytes);
        if (bytes)
        {
            assert(image.lines.IsAllocated());
            MemBlockHandle* lines = image.lines.Get<MemBlockHandle*>();
            for (unsigned y = 0; y < image.height; ++y)
                assert(fwrite(lines[y].GetPtr(), 1, pitch, stdout) == pitch);
        }
        delete decoder;
        for (size_t n = 0; n < allocations.size(); ++n) free(allocations[n]);
        allocations.clear();
    }
    return 0;
}
