#ifndef _PNG_H_
#define _PNG_H_

#include "Decoder.h"

#define PNG_SIGNATURE_LENGTH 8
#define PNG_MAX_ROW_BYTES 4096
#define PNG_WINDOW_SIZE 32768u

// Streaming decoder. Its bounded workspace is shared with the other decoders
// by ImageDecoderUnion; no compressed file or full RGB image is buffered.
class PngDecoder : public ImageDecoder
{
public:
    PngDecoder();
    virtual void Process(uint8_t* data, size_t dataLength) override;

private:
    enum ParseState { Signature, ChunkHeader, ChunkData, ChunkCRC };
    enum ChunkKind { HeaderChunk, PaletteChunk, TransparencyChunk, DataChunk, EndChunk, OtherChunk };
    enum InflateState
    {
        ZlibHeader, BlockHeader, StoredLength, StoredBytes, DynamicHeader,
        CodeLengths, TreeLengths, RepeatLength, Literal, LengthExtra,
        Distance, DistanceExtra, Adler, Inflated
    };
    struct Huffman
    {
        uint16_t counts[16];
        uint16_t symbols[288];
    };

    bool FinishChunk();
    bool ReadHeader();
    bool AllocateImage();
    void InflateByte(uint8_t value);
    unsigned ReadBits(unsigned count);
    bool BuildTree(Huffman& tree, const uint8_t* lengths, unsigned count, bool allowSingle);
    int DecodeSymbol(Huffman& tree);
    void EmitByte(uint8_t value);
    void EmitRow();
    void EmitLine(unsigned y);
    uint8_t MapPixel(unsigned sourceX, unsigned x, unsigned y);
    void UpdateCRC(uint8_t value);

    ParseState parseState;
    ChunkKind chunkKind;
    InflateState inflateState;
    uint8_t input[13];
    unsigned inputPosition;
    uint32_t chunkLength, chunkPosition, crc, receivedCRC;
    unsigned crcPosition;
    bool haveHeader, havePalette, haveTransparency, haveData, endedData;
    unsigned sourceWidth, sourceHeight, bitDepth, colourType, channels;
    unsigned rowBytes, filterBytes, rowPosition, rowIndex;
    uint8_t filter, previousUp[4];
    uint8_t row[PNG_MAX_ROW_BYTES];
    uint8_t palette[256 * 3], alpha[256];
    unsigned paletteSize;
    uint16_t transparent[3];

    uint32_t bits;
    unsigned bitCount;
    bool finalBlock;
    unsigned storedRemaining, windowPosition, windowAvailable;
    uint8_t window[PNG_WINDOW_SIZE];
    Huffman literalTree, distanceTree, codeTree;
    uint8_t lengths[320];
    unsigned literalCount, distanceCount, codeCount, lengthIndex;
    unsigned repeatSymbol, matchLength, matchDistance, extraBits;
    uint32_t adlerLow, adlerHigh, receivedAdler;
    unsigned adlerPosition;
};

#endif
