// Reuse host allocator/video scaffolding; exercise the production decoder.
#define main palette_test_main
#include "tandy_gif.cpp"
#undef main
#include <time.h>
int main(int argc, char** argv)
{
    assert(argc == 5);
    FILE* in = fopen(argv[1], "rb"); assert(in);
    std::vector<uint8_t> bytes;
    int c; while ((c = fgetc(in)) != EOF) bytes.push_back(c);
    fclose(in);
    const size_t chunk = atoi(argv[3]);
    const int repeats = atoi(argv[4]);
    assert(chunk && repeats > 0);
    clock_t start = clock();
    for (int repeat = 0; repeat < repeats; ++repeat)
    {
        TestSurface surface(DrawSurface::Format_4BPP_Packed);
        TestVideo video(&surface); Platform::video = &video;
        Image image; image.width = image.height = 0;
        GifDecoder decoder; decoder.Begin(&image, false);
        for (size_t pos = 0; pos < bytes.size(); pos += chunk)
            decoder.Process(&bytes[pos], std::min(chunk, bytes.size() - pos));
        assert(decoder.GetState() == ImageDecoder::Success);
        if (!repeat)
        {
            FILE* out = fopen(argv[2], "wb"); assert(out);
            MemBlockHandle* lines = image.lines.Get<MemBlockHandle*>();
            for (int y = 0; y < image.height; ++y)
                fwrite(lines[y].GetPtr(), 1, image.pitch, out);
            fclose(out);
        }
        for (size_t i = 0; i < allocations.size(); ++i) free(allocations[i]);
        allocations.clear();
    }
    printf("%s: %d decodes %.6f seconds; decoder %zu bytes\n", argv[1], repeats,
        double(clock() - start) / CLOCKS_PER_SEC, sizeof(GifDecoder));
}
