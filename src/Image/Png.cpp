#include <string.h>
#include "Png.h"
#include "Image.h"
#include "../Platform.h"
#include "../Colour.h"
#include "../Memory/Memory.h"

static const uint8_t pngSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
// The actual DEFLATE permutation, kept explicit to make header decoding clear.
static const uint8_t deflateCodeOrder[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
static const uint16_t lengthBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t lengthExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t distanceBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t distanceExtra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static const uint32_t crcNibble[16] = {
    0x00000000UL,0x1db71064UL,0x3b6e20c8UL,0x26d930acUL,
    0x76dc4190UL,0x6b6b51f4UL,0x4db26158UL,0x5005713cUL,
    0xedb88320UL,0xf00f9344UL,0xd6d6a3e8UL,0xcb61b38cUL,
    0x9b64c2b0UL,0x86d3d2d4UL,0xa00ae278UL,0xbdbdf21cUL
};

PngDecoder::PngDecoder()
    : parseState(Signature), chunkKind(OtherChunk), inflateState(ZlibHeader),
      inputPosition(0), chunkLength(0), chunkPosition(0), crc(0), receivedCRC(0), crcPosition(0),
      haveHeader(false), havePalette(false), haveTransparency(false), haveData(false), endedData(false),
      sourceWidth(0), sourceHeight(0), bitDepth(0), colourType(0), channels(0),
      rowBytes(0), filterBytes(0), rowPosition(0), rowIndex(0), paletteSize(0),
      bits(0), bitCount(0), finalBlock(false), storedRemaining(0), windowPosition(0), windowAvailable(0),
      literalCount(0), distanceCount(0), codeCount(0), lengthIndex(0), repeatSymbol(0),
      matchLength(0), matchDistance(0), extraBits(0), adlerLow(1), adlerHigh(0), receivedAdler(0), adlerPosition(0)
{
    memset(alpha, 255, sizeof(alpha));
    memset(transparent, 0, sizeof(transparent));
}

void PngDecoder::UpdateCRC(uint8_t value)
{
    crc ^= value;
    crc = (crc >> 4) ^ crcNibble[crc & 15];
    crc = (crc >> 4) ^ crcNibble[crc & 15];
}

void PngDecoder::Process(uint8_t* data, size_t dataLength)
{
    while (dataLength && state == Decoding)
    {
        uint8_t value = *data++;
        --dataLength;
        switch (parseState)
        {
        case Signature:
            if (value != pngSignature[inputPosition++]) { state = Error; return; }
            if (inputPosition == 8) { inputPosition = 0; parseState = ChunkHeader; }
            break;
        case ChunkHeader:
            input[inputPosition++] = value;
            if (inputPosition != 8) break;
            chunkLength = ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) | ((uint32_t)input[2] << 8) | input[3];
            if (chunkLength > 0x7fffffffUL) { state = Error; return; }
            crc = 0xffffffffUL;
            for (unsigned n = 4; n < 8; ++n)
            {
                if (!((input[n] >= 'A' && input[n] <= 'Z') || (input[n] >= 'a' && input[n] <= 'z'))) { state = Error; return; }
                UpdateCRC(input[n]);
            }
            if (input[6] & 32) { state = Error; return; }
            if (!memcmp(input + 4, "IHDR", 4)) chunkKind = HeaderChunk;
            else if (!memcmp(input + 4, "PLTE", 4)) chunkKind = PaletteChunk;
            else if (!memcmp(input + 4, "tRNS", 4)) chunkKind = TransparencyChunk;
            else if (!memcmp(input + 4, "IDAT", 4)) chunkKind = DataChunk;
            else if (!memcmp(input + 4, "IEND", 4)) chunkKind = EndChunk;
            else if (!(input[4] & 32)) { state = Error; return; }
            else chunkKind = OtherChunk;
            if (!haveHeader && chunkKind != HeaderChunk) { state = Error; return; }
            if (haveData && chunkKind != DataChunk) endedData = true;
            if ((chunkKind == HeaderChunk && (haveHeader || chunkLength != 13)) ||
                (chunkKind == PaletteChunk && (havePalette || haveTransparency || haveData || !chunkLength || chunkLength > 768 || chunkLength % 3 || colourType == 0 || colourType == 4)) ||
                (chunkKind == TransparencyChunk && (haveTransparency || haveData ||
                    !((colourType == 3 && havePalette && chunkLength && chunkLength <= paletteSize) ||
                      (colourType == 0 && chunkLength == 2) || (colourType == 2 && chunkLength == 6)))) ||
                (chunkKind == DataChunk && (endedData || (colourType == 3 && !havePalette))) ||
                (chunkKind == EndChunk && chunkLength)) { state = Error; return; }
            if (chunkKind == DataChunk) haveData = true;
            chunkPosition = 0;
            inputPosition = crcPosition = 0;
            receivedCRC = 0;
            parseState = chunkLength ? ChunkData : ChunkCRC;
            break;
        case ChunkData:
            UpdateCRC(value);
            if (chunkKind == HeaderChunk) input[(unsigned)chunkPosition] = value;
            else if (chunkKind == PaletteChunk) palette[(unsigned)chunkPosition] = value;
            else if (chunkKind == TransparencyChunk)
            {
                if (colourType == 3) alpha[(unsigned)chunkPosition] = value;
                else input[(unsigned)chunkPosition] = value;
            }
            else if (chunkKind == DataChunk) InflateByte(value);
            if (++chunkPosition == chunkLength) parseState = ChunkCRC;
            break;
        case ChunkCRC:
            receivedCRC = (receivedCRC << 8) | value;
            if (++crcPosition == 4)
            {
                if (receivedCRC != (crc ^ 0xffffffffUL) || !FinishChunk()) { state = Error; return; }
                inputPosition = 0;
                parseState = ChunkHeader;
            }
            break;
        }
    }
}

bool PngDecoder::FinishChunk()
{
    if (chunkKind == HeaderChunk)
    {
        if (!ReadHeader()) return false;
        haveHeader = true;
        if (onlyDownloadDimensions) state = Success;
        else if (!AllocateImage()) return false;
    }
    else if (chunkKind == PaletteChunk)
    {
        paletteSize = (unsigned)(chunkLength / 3);
        if (colourType == 3 && paletteSize > (1u << bitDepth)) return false;
        havePalette = true;
    }
    else if (chunkKind == TransparencyChunk)
    {
        if (colourType != 3)
            for (unsigned n = 0; n < (colourType == 0 ? 1u : 3u); ++n)
            {
                transparent[n] = ((unsigned)input[n * 2] << 8) | input[n * 2 + 1];
                if (transparent[n] >= (1u << bitDepth)) return false;
            }
        haveTransparency = true;
    }
    else if (chunkKind == EndChunk)
    {
        if (!haveData || inflateState != Inflated || rowIndex != sourceHeight || rowPosition) return false;
        linesDecoded = (int)outputImage->height;
        state = Success;
    }
    return true;
}

bool PngDecoder::ReadHeader()
{
    uint32_t width = ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) | ((uint32_t)input[2] << 8) | input[3];
    uint32_t height = ((uint32_t)input[4] << 24) | ((uint32_t)input[5] << 16) | ((uint32_t)input[6] << 8) | input[7];
    bitDepth = input[8]; colourType = input[9];
    if (!width || !height || width > 32767 || height > 32767 || input[10] || input[11] || input[12] > 1) return false;
    if (colourType == 0) channels = 1;
    else if (colourType == 2) channels = 3;
    else if (colourType == 3) channels = 1;
    else if (colourType == 4) channels = 2;
    else if (colourType == 6) channels = 4;
    else return false;
    if (!((bitDepth == 8) || (bitDepth == 16 && colourType != 3) ||
        ((bitDepth == 1 || bitDepth == 2 || bitDepth == 4) && (colourType == 0 || colourType == 3)))) return false;
    sourceWidth = (unsigned)width; sourceHeight = (unsigned)height;
    CalculateImageDimensions((int)sourceWidth, (int)sourceHeight);
    if (onlyDownloadDimensions) return true;
    // Adam7 and 16-bit samples retain layout dimensions but are not rendered.
    if (bitDepth == 16 || input[12]) return false;
    uint32_t bytes = (width * channels * bitDepth + 7) / 8;
    if (bytes > PNG_MAX_ROW_BYTES || !outputImage->width || outputImage->width > 1024 || !outputImage->height || outputImage->height > 4096) return false;
    rowBytes = (unsigned)bytes;
    filterBytes = (channels * bitDepth + 7) / 8;
    memset(row, 0, rowBytes);
    return true;
}

bool PngDecoder::AllocateImage()
{
    outputImage->pitch = outputImage->bpp == 1 ? (outputImage->width + 7) / 8 : outputImage->width;
    outputImage->lines = MemoryManager::pageBlockAllocator.Allocate((uint16_t)(sizeof(MemBlockHandle) * outputImage->height));
    if (!outputImage->lines.IsAllocated()) return false;
    for (unsigned y = 0; y < outputImage->height; ++y)
    {
        MemBlockHandle line = MemoryManager::pageBlockAllocator.Allocate(outputImage->pitch);
        if (!line.IsAllocated()) { outputImage->lines.type = MemBlockHandle::Unallocated; return false; }
        uint8_t* pixels = line.Get<uint8_t*>();
        if (!pixels) { outputImage->lines.type = MemBlockHandle::Unallocated; return false; }
        memset(pixels, outputImage->bpp == 1 ? (Platform::video->colourScheme.pageColour ? 255 : 0) : TRANSPARENT_COLOUR_VALUE, outputImage->pitch);
        line.Commit();
        MemBlockHandle* lines = outputImage->lines.Get<MemBlockHandle*>();
        if (!lines) { outputImage->lines.type = MemBlockHandle::Unallocated; return false; }
        lines[y] = line;
        outputImage->lines.Commit();
    }
    return true;
}

unsigned PngDecoder::ReadBits(unsigned count)
{
    unsigned result = (unsigned)(bits & ((1UL << count) - 1));
    bits >>= count; bitCount -= count;
    return result;
}

bool PngDecoder::BuildTree(Huffman& tree, const uint8_t* values, unsigned count, bool allowSingle)
{
    memset(tree.counts, 0, sizeof(tree.counts));
    for (unsigned n = 0; n < count; ++n)
    {
        if (values[n] > 15) return false;
        ++tree.counts[values[n]];
    }
    long left = 1;
    for (unsigned len = 1; len <= 15; ++len)
    {
        left = (left << 1) - tree.counts[len];
        if (left < 0) return false;
    }
    if (left && !(allowSingle && (tree.counts[0] == count || (tree.counts[1] == 1 && tree.counts[0] == count - 1)))) return false;
    uint16_t offsets[16]; offsets[1] = 0;
    for (unsigned len = 1; len < 15; ++len) offsets[len + 1] = offsets[len] + tree.counts[len];
    for (unsigned n = 0; n < count; ++n) if (values[n]) tree.symbols[offsets[values[n]]++] = (uint16_t)n;
    return true;
}

int PngDecoder::DecodeSymbol(Huffman& tree)
{
    unsigned code = 0, first = 0, index = 0;
    for (unsigned len = 1; len <= 15; ++len)
    {
        if (bitCount < len) return -1;
        code |= (unsigned)((bits >> (len - 1)) & 1);
        unsigned count = tree.counts[len];
        if (code >= first && code - first < count)
        {
            ReadBits(len);
            return tree.symbols[index + code - first];
        }
        index += count; first = (first + count) << 1; code <<= 1;
    }
    state = Error;
    return -1;
}

void PngDecoder::InflateByte(uint8_t value)
{
    if (inflateState == Inflated || bitCount > 24) { state = Error; return; }
    bits |= (uint32_t)value << bitCount; bitCount += 8;
    while (state == Decoding)
    {
        int symbol;
        switch (inflateState)
        {
        case ZlibHeader:
            if (bitCount < 16) return;
            {
                unsigned cmf = ReadBits(8), flg = ReadBits(8);
                if ((cmf & 15) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 || (flg & 32)) { state = Error; return; }
            }
            inflateState = BlockHeader;
            break;
        case BlockHeader:
            if (bitCount < 3) return;
            finalBlock = ReadBits(1) != 0;
            symbol = (int)ReadBits(2);
            if (symbol == 0) { ReadBits(bitCount & 7); inflateState = StoredLength; }
            else if (symbol == 1)
            {
                for (unsigned n = 0; n < 288; ++n) lengths[n] = n < 144 ? 8 : n < 256 ? 9 : n < 280 ? 7 : 8;
                BuildTree(literalTree, lengths, 288, false);
                memset(lengths, 5, 32); BuildTree(distanceTree, lengths, 32, false);
                inflateState = Literal;
            }
            else if (symbol == 2) inflateState = DynamicHeader;
            else { state = Error; return; }
            break;
        case StoredLength:
            if (bitCount < 32) return;
            storedRemaining = ReadBits(16);
            if ((storedRemaining ^ ReadBits(16)) != 65535u) { state = Error; return; }
            inflateState = StoredBytes;
            break;
        case StoredBytes:
            if (!storedRemaining) { inflateState = finalBlock ? Adler : BlockHeader; break; }
            if (bitCount < 8) return;
            EmitByte((uint8_t)ReadBits(8)); --storedRemaining;
            break;
        case DynamicHeader:
            if (bitCount < 14) return;
            literalCount = ReadBits(5) + 257; distanceCount = ReadBits(5) + 1; codeCount = ReadBits(4) + 4;
            if (literalCount > 286 || distanceCount > 32) { state = Error; return; }
            memset(lengths, 0, 19); lengthIndex = 0; inflateState = CodeLengths;
            break;
        case CodeLengths:
            if (lengthIndex == codeCount)
            {
                if (!BuildTree(codeTree, lengths, 19, false)) { state = Error; return; }
                lengthIndex = 0; inflateState = TreeLengths; break;
            }
            if (bitCount < 3) return;
            lengths[deflateCodeOrder[lengthIndex++]] = (uint8_t)ReadBits(3);
            break;
        case TreeLengths:
            if (lengthIndex == literalCount + distanceCount)
            {
                if (!lengths[256] || !BuildTree(literalTree, lengths, literalCount, true) ||
                    !BuildTree(distanceTree, lengths + literalCount, distanceCount, true)) { state = Error; return; }
                inflateState = Literal; break;
            }
            symbol = DecodeSymbol(codeTree); if (symbol < 0) return;
            if (symbol < 16) lengths[lengthIndex++] = (uint8_t)symbol;
            else
            {
                if (symbol == 16 && !lengthIndex) { state = Error; return; }
                repeatSymbol = (unsigned)symbol; inflateState = RepeatLength;
            }
            break;
        case RepeatLength:
            extraBits = repeatSymbol == 16 ? 2 : repeatSymbol == 17 ? 3 : 7;
            if (bitCount < extraBits) return;
            {
                unsigned count = ReadBits(extraBits) + (repeatSymbol == 18 ? 11 : 3);
                uint8_t previous = repeatSymbol == 16 ? lengths[lengthIndex - 1] : 0;
                if (count > literalCount + distanceCount - lengthIndex) { state = Error; return; }
                while (count--) lengths[lengthIndex++] = previous;
            }
            inflateState = TreeLengths; break;
        case Literal:
            symbol = DecodeSymbol(literalTree); if (symbol < 0) return;
            if (symbol < 256) EmitByte((uint8_t)symbol);
            else if (symbol == 256)
            {
                inflateState = finalBlock ? Adler : BlockHeader;
                if (finalBlock) ReadBits(bitCount & 7);
            }
            else if (symbol <= 285)
            {
                matchLength = lengthBase[symbol - 257]; extraBits = lengthExtra[symbol - 257]; inflateState = LengthExtra;
            }
            else { state = Error; return; }
            break;
        case LengthExtra:
            if (bitCount < extraBits) return;
            matchLength += ReadBits(extraBits); inflateState = Distance; break;
        case Distance:
            symbol = DecodeSymbol(distanceTree); if (symbol < 0) return;
            if (symbol > 29) { state = Error; return; }
            matchDistance = distanceBase[symbol]; extraBits = distanceExtra[symbol]; inflateState = DistanceExtra;
            break;
        case DistanceExtra:
            if (bitCount < extraBits) return;
            matchDistance += ReadBits(extraBits);
            if (matchDistance > windowAvailable) { state = Error; return; }
            while (matchLength-- && state == Decoding) EmitByte(window[(windowPosition - matchDistance) & (PNG_WINDOW_SIZE - 1)]);
            inflateState = Literal; break;
        case Adler:
            if (bitCount < 8) return;
            receivedAdler = (receivedAdler << 8) | ReadBits(8);
            if (++adlerPosition == 4)
            {
                if (receivedAdler != ((adlerHigh << 16) | adlerLow) || bitCount || rowIndex != sourceHeight || rowPosition) { state = Error; return; }
                inflateState = Inflated;
                return;
            }
            break;
        case Inflated:
            return;
        }
    }
}

void PngDecoder::EmitByte(uint8_t value)
{
    if (rowIndex >= sourceHeight) { state = Error; return; }
    window[windowPosition] = value;
    windowPosition = (windowPosition + 1) & (PNG_WINDOW_SIZE - 1);
    if (windowAvailable < PNG_WINDOW_SIZE) ++windowAvailable;
    adlerLow += value; if (adlerLow >= 65521UL) adlerLow -= 65521UL;
    adlerHigh += adlerLow; if (adlerHigh >= 65521UL) adlerHigh -= 65521UL;
    if (!rowPosition)
    {
        filter = value;
        if (filter > 4) { state = Error; return; }
        memset(previousUp, 0, sizeof(previousUp));
    }
    else
    {
        unsigned x = rowPosition - 1;
        unsigned left = x >= filterBytes ? row[x - filterBytes] : 0;
        unsigned up = row[x], upperLeft = previousUp[x % filterBytes];
        previousUp[x % filterBytes] = (uint8_t)up;
        if (filter == 1) value = (uint8_t)(value + left);
        else if (filter == 2) value = (uint8_t)(value + up);
        else if (filter == 3) value = (uint8_t)(value + ((left + up) >> 1));
        else if (filter == 4)
        {
            int p = (int)left + (int)up - (int)upperLeft;
            int a = p - (int)left, b = p - (int)up, c = p - (int)upperLeft;
            if (a < 0) a = -a;
            if (b < 0) b = -b;
            if (c < 0) c = -c;
            value = (uint8_t)(value + (a <= b && a <= c ? left : b <= c ? up : upperLeft));
        }
        row[x] = value;
    }
    if (++rowPosition == rowBytes + 1)
    {
        EmitRow(); rowPosition = 0; ++rowIndex;
    }
}

void PngDecoder::EmitRow()
{
    unsigned first = (unsigned)((uint32_t)rowIndex * outputImage->height / sourceHeight);
    unsigned last = (unsigned)((uint32_t)(rowIndex + 1) * outputImage->height / sourceHeight);
    for (unsigned y = first; y < last && state == Decoding; ++y) EmitLine(y);
    // Publish only after IEND and all checksums pass. The existing progressive
    // image border would otherwise show through transparent edge pixels.
}

uint8_t PngDecoder::MapPixel(unsigned sourceX, unsigned x, unsigned y)
{
    unsigned red, green, blue, opacity = 255;
    unsigned sample;
    if (bitDepth < 8)
    {
        unsigned bit = sourceX * bitDepth;
        sample = (row[bit >> 3] >> (8 - bitDepth - (bit & 7))) & ((1u << bitDepth) - 1);
    }
    else sample = row[sourceX * channels];
    if (colourType == 3)
    {
        if (sample >= paletteSize) { state = Error; return 0; }
        red = palette[sample * 3]; green = palette[sample * 3 + 1]; blue = palette[sample * 3 + 2]; opacity = alpha[sample];
    }
    else if (colourType == 0 || colourType == 4)
    {
        red = green = blue = bitDepth < 8 ? sample * 255 / ((1u << bitDepth) - 1) : sample;
        if (colourType == 4) opacity = row[sourceX * 2 + 1];
        else if (haveTransparency && sample == transparent[0]) opacity = 0;
    }
    else
    {
        unsigned index = sourceX * channels;
        red = row[index]; green = row[index + 1]; blue = row[index + 2];
        if (colourType == 6) opacity = row[index + 3];
        else if (haveTransparency && red == transparent[0] && green == transparent[1] && blue == transparent[2]) opacity = 0;
    }
    if (outputImage->bpp == 1)
    {
        unsigned grey = (unsigned)(((uint32_t)red * 76 + (uint32_t)green * 150 + (uint32_t)blue * 30) >> 8);
        // Monochrome image storage has no transparency mask. Composite against
        // the page background before the existing ordered monochrome dither.
        grey = (unsigned)(((uint32_t)grey * opacity + (uint32_t)(Platform::video->colourScheme.pageColour ? 255 : 0) * (255 - opacity) + 127) / 255);
        return grey > greyDitherMatrix[(y & 15) * 16 + (x & 15)] ? 1 : 0;
    }
    // The image blitter supports binary transparency. Ordered alpha coverage
    // preserves the page under fully transparent and partly covered pixels.
    unsigned threshold = (unsigned)(colourDitherMatrix[(y & 3) * 4 + (x & 3)] + 32) * 4 + 8;
    if (!opacity || (opacity < 255 && opacity <= threshold)) return TRANSPARENT_COLOUR_VALUE;
    return Platform::video->paletteLUT[RGB332(red, green, blue)];
}

void PngDecoder::EmitLine(unsigned y)
{
    MemBlockHandle* lines = outputImage->lines.Get<MemBlockHandle*>();
    if (!lines) { state = Error; return; }
    MemBlockHandle line = lines[y];
    uint8_t* pixels = line.Get<uint8_t*>();
    if (!pixels) { state = Error; return; }
    if (outputImage->bpp == 1) memset(pixels, 0, outputImage->pitch);
    // An accumulator avoids a 32-bit division for every output pixel on 8088.
    unsigned sourceX = 0;
    uint32_t fraction = 0;
    for (unsigned x = 0; x < outputImage->width; ++x)
    {
        uint8_t value = MapPixel(sourceX, x, y);
        if (state == Error) return;
        if (outputImage->bpp == 1) { if (value) pixels[x >> 3] |= 128 >> (x & 7); }
        else pixels[x] = value;
        fraction += sourceWidth;
        while (fraction >= outputImage->width) { fraction -= outputImage->width; ++sourceX; }
    }
    line.Commit();
}
