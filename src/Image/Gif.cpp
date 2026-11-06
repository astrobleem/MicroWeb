#include "Gif.h"
#include "Image.h"
#include "../Platform.h"
#include "../Colour.h"
#include "../Memory/Memory.h"
#include "../Page.h"
#include "../App.h"
#include "../Draw/Surface.h"
#include <stdio.h>

#ifdef _WIN32
#define DEBUG_MESSAGE(...) printf(__VA_ARGS__);
#else
#define DEBUG_MESSAGE(...)
#endif

#define BLOCK_TYPE_EXTENSION_INTRODUCER 0x21
#define BLOCK_TYPE_IMAGE_DESCRIPTOR 0x2C
#define BLOCK_TYPE_TRAILER 0x3B
#define BLOCK_TYPE_GRAPHIC_CONTROL_EXTENSION 0xF9

GifDecoder::GifDecoder() 
{
	internalState = ParseHeader;
	lineBufferSkipCount = 0;
	transparentColourIndex = -1;
	activePaletteSize = 0;
	useTandyPalette = false;
}

void GifDecoder::Process(uint8_t* data, size_t dataLength)
{
	if(state != ImageDecoder::Decoding)
	{
		return;
	}
	
	while(dataLength > 0)
	{
		switch(internalState)
		{
			case ParseHeader:
			{
				if(FillStruct(&data, dataLength, &header, sizeof(Header)))
				{
					// Header structure is complete
					if(memcmp(header.versionTag, "GIF89a", 6) && memcmp(header.versionTag, "GIF87a", 6))
					{
						// Not a GIF89a
						state = ImageDecoder::Error;
						return;
					}

					// If the image width is wider than the buffer, we will skip every N pixels
					lineBufferDivider = 1;
					while (header.width / lineBufferDivider > GIF_LINE_BUFFER_MAX_SIZE)
					{
						lineBufferDivider++;
					}
					
					CalculateImageDimensions(header.width, header.height);

					if (onlyDownloadDimensions)
					{
						state = ImageDecoder::Success;
						return;
					}

					if (outputImage->bpp == 1)
					{
						outputImage->pitch = (outputImage->width + 7) / 8;
					}
					else // 8bpp
					{
						outputImage->pitch = outputImage->width;
					}

					outputImage->lines = MemoryManager::pageBlockAllocator.Allocate(sizeof(MemBlockHandle) * outputImage->height);
					if(!outputImage->lines.IsAllocated())
					{
						// Allocation error
						DEBUG_MESSAGE("Could not allocate!\n");
						state = ImageDecoder::Error;
						return;
					}

					MemBlockHandle* lines = outputImage->lines.Get<MemBlockHandle*>();

					for (int j = 0; j < outputImage->height; j++)
					{
						lines[j] = MemoryManager::pageBlockAllocator.Allocate(outputImage->pitch);

						if (!lines[j].IsAllocated())
						{
							// Allocation error
							DEBUG_MESSAGE("Could not allocate!\n");
							outputImage->lines.type = MemBlockHandle::Unallocated;
							state = ImageDecoder::Error;
							return;
						}
					}

					outputImage->lines.Commit();
					for (int j = 0; j < outputImage->height; j++)
					{
						lines = outputImage->lines.Get<MemBlockHandle*>();
						MemBlockHandle line = lines[j];
						void* pixels = line.GetPtr();
						if (pixels)
						{
							memset(pixels, TRANSPARENT_COLOUR_VALUE, outputImage->pitch);
							line.Commit();
						}
					}
					
					backgroundColour = header.backgroundColour;
					
					DEBUG_MESSAGE("Image is %d x %d\n", outputImage->width, outputImage->height);
					
					if(header.fields & (1 << 7))
					{
						paletteSize = (1 << ((header.fields & 0x7) + 1));
						DEBUG_MESSAGE("Image has a palette of %d colours\n", (int) paletteSize);
						
						internalState = ParsePalette;
						paletteIndex = 0;
					}
					else
					{
						internalState = ParseDataBlock;
					}
				}
			}
			break;
			
			case ParsePalette:
			{
				if(FillStruct(&data, dataLength, rgb, 3))
				{
					//DEBUG_MESSAGE("RGB index %d: %x %x %x\n", paletteIndex, (int)(rgb[0]), (int)(rgb[1]), (int)(rgb[2]));

					//uint8_t grey = (uint8_t)(((uint16_t)rgb[0] * 76 + (uint16_t)rgb[1] * 150 + (uint16_t)rgb[2] * 30) >> 8);
					//palette[paletteIndex++] = grey;
					palette[paletteIndex * 3] = rgb[0];
					palette[paletteIndex * 3 + 1] = rgb[1];
					palette[paletteIndex * 3 + 2] = rgb[2];
					paletteIndex++;

					if(paletteIndex == paletteSize)
					{
						GeneratePaletteLUT(paletteSize);

						internalState = ParseDataBlock;
					}
				}
			}
			break;
			
			case ParseDataBlock:
			{
				uint8_t blockType = NextByte(&data, dataLength);

				switch(blockType)
				{
					case BLOCK_TYPE_IMAGE_DESCRIPTOR:
					internalState = ParseImageDescriptor;
					break;
					
					case BLOCK_TYPE_TRAILER:
					// End of GIF
					state = ImageDecoder::Success;
					return;
					
					case BLOCK_TYPE_EXTENSION_INTRODUCER:
					internalState = ParseExtension;
					break;
					
					default:
						DEBUG_MESSAGE("Invalid block type: %x\n", (int)(blockType));
					state = ImageDecoder::Error;
					return;
				}
			}
			break;
			
			case ParseImageDescriptor:
			{
				if(FillStruct(&data, dataLength, &imageDescriptor, sizeof(ImageDescriptor)))
				{
					DEBUG_MESSAGE("Image: %d, %d %d, %d\n", imageDescriptor.x, imageDescriptor.y, imageDescriptor.width, imageDescriptor.height);
					
					drawX = drawY = 0;
					outputLine = 0;

					linesProcessed = 0;
					lineBufferSize = 0;
					lineBufferFlushCount = 0;

					if (imageDescriptor.fields & 0x80)
					{
						internalState = ParseLocalColourTable;
						paletteIndex = 0;
						localColourTableLength = 1 << ((imageDescriptor.fields & 7) + 1);
					}
					else
					{
						internalState = ParseLZWCodeSize;
					}
				}
			}
			break;

			case ParseLocalColourTable:
			{
				if (FillStruct(&data, dataLength, &rgb, 3))
				{
					palette[paletteIndex * 3] = rgb[0];
					palette[paletteIndex * 3 + 1] = rgb[1];
					palette[paletteIndex * 3 + 2] = rgb[2];

					paletteIndex++;

					if (paletteIndex == localColourTableLength)
					{
						GeneratePaletteLUT(localColourTableLength);

						internalState = ParseLZWCodeSize;
					}
				}
			}
			break;

			case ParseLZWCodeSize:
			{
				lzwCodeSize = NextByte(&data, dataLength);
				DEBUG_MESSAGE("LZW code size: %d\n", lzwCodeSize);

				if (lzwCodeSize < 2 || lzwCodeSize > 8)
				{
					state = ImageDecoder::Error;
					return;
				}

				// Init LZW vars
				code = 0;
				clearCode = 1 << lzwCodeSize;
				stopCode = clearCode + 1;
				codeLength = resetCodeLength = lzwCodeSize + 1;
				codeBit = 0;
				prev = -1;
				ClearDictionary();

				internalState = ParseImageSubBlockSize;
			}
			break;
			
			case ParseImageSubBlockSize:
			{
				imageSubBlockSize = NextByte(&data, dataLength);
				DEBUG_MESSAGE("Sub block size: %d bytes\n", imageSubBlockSize);
				if(imageSubBlockSize)
				{
					internalState = ParseImageSubBlock;
				}
				else
				{
					internalState = ParseDataBlock;

					// HACK: Finish decoding after first frame
					state = ImageDecoder::Success;
					return;
				}
			}
			break;
			
			case ParseImageSubBlock:
			{
				while (imageSubBlockSize && dataLength)
				{
					imageSubBlockSize--;
					unsigned int dataByte = NextByte(&data, dataLength);
					int bits = 8;
					// codeBit == -1 means EOI: discard padding until the terminator.
					if (codeBit < 0) continue;
					while (bits)
					{
						// Consume a whole group of bits, never a bit-at-a-time loop.
						// The accumulator holds at most 12 bits, so no 32-bit shifts
						// (or 19-bit reservoir) are needed on the 8088.
						int take = codeLength - codeBit;
						if (take > bits) take = bits;
						code |= (dataByte & ((1U << take) - 1)) << codeBit;
						dataByte >>= take;
						bits -= take;
						codeBit += take;
						if (codeBit < codeLength) continue;
						int currentCode = code;
						code = 0;
						codeBit = 0;
						if (currentCode == clearCode)
						{
							codeLength = resetCodeLength;
							// Literal entries never change; only discard composite entries.
							dictionaryIndex = clearCode + 2;
							prev = -1;
							continue;
						}
						if (currentCode == stopCode)
						{
							codeBit = -1;
							break;
						}
						if (currentCode > dictionaryIndex ||
							(prev < 0 && currentCode >= clearCode))
						{
							state = ImageDecoder::Error;
							return;
						}

						uint8_t stack[1024];
						bool special = currentCode == dictionaryIndex;
						int stackSize = special ? 1 : 0;
						int ptr = special ? prev : currentCode;
						while (ptr != -1)
						{
							if (stackSize >= 1024)
							{
								state = ImageDecoder::Error;
								return;
							}
							stack[stackSize++] = dictionary[ptr].byte;
							ptr = dictionary[ptr].prev;
						}
						// The first character is already at the top of the stack.
						// Reuse it instead of walking the prefix chain twice or
						// spending another 4096 bytes on a first-character cache.
						uint8_t first = stack[stackSize - 1];
						if (special) stack[0] = first;
						if (prev >= 0 && dictionaryIndex < GIF_MAX_DICTIONARY_ENTRIES)
						{
							dictionary[dictionaryIndex].byte = first;
							dictionary[dictionaryIndex++].prev = prev;
							if (dictionaryIndex == (1 << codeLength) && codeLength < 12)
								codeLength++;
						}
						prev = currentCode;
						while (stackSize)
						{
							if (lineBufferSkipCount == lineBufferDivider - 1)
							{
								lineBuffer[lineBufferSize++] = stack[stackSize - 1];
								lineBufferSkipCount = 0;
							}
							else
							{
								lineBufferSkipCount++;
							}

							lineBufferFlushCount++;
							stackSize--;

							if (lineBufferFlushCount == imageDescriptor.width)
							{
								ProcessLineBuffer();
								lineBufferSize = 0;
								lineBufferFlushCount = 0;
							}
						}
					}
				}
				if (!imageSubBlockSize) internalState = ParseImageSubBlockSize;
			}
			break;

			case ParseExtension:
			{
				if(FillStruct(&data, dataLength, &extensionHeader, sizeof(ExtensionHeader)))
				{
					DEBUG_MESSAGE("Extension: %x Size: %d\n", (int) extensionHeader.code, (int) extensionHeader.size);

					if (extensionHeader.code == BLOCK_TYPE_GRAPHIC_CONTROL_EXTENSION)
					{
						internalState = ParseGraphicControlExtension;
					}
					else
					{
						internalState = ParseExtensionContents;
					}
				}
			}
			break;
			
			case ParseExtensionContents:
			{
				if(SkipBytes(&data, dataLength, extensionHeader.size))
				{
					internalState = ParseExtensionSubBlockSize;
				}
			}
			break;

			case ParseGraphicControlExtension:
			{
				if (FillStruct(&data, dataLength, &graphicControlExtension, sizeof(GraphicControlExtension)))
				{
					if (graphicControlExtension.packedFields & 1)
					{
						transparentColourIndex = graphicControlExtension.transparentColourIndex;
						paletteLUT[transparentColourIndex] = TRANSPARENT_COLOUR_VALUE;
						if (activePaletteSize) GeneratePaletteLUT(activePaletteSize);
					}
					internalState = ParseExtensionSubBlockSize;
				}
			}
			break;
			
			case ParseExtensionSubBlockSize:
			{
				extensionSubBlockSize = NextByte(&data, dataLength);
				if(extensionSubBlockSize > 0)
				{
					internalState = ParseExtensionSubBlock;
				}
				else
				{
					internalState = ParseDataBlock;
				}
			}
			break;
			
			case ParseExtensionSubBlock:
			{
				if(SkipBytes(&data, dataLength, extensionSubBlockSize))
				{
					internalState = ParseExtensionSubBlockSize;
				}
			}
			break;
		}
	}
}

// Native RGBI GIFs already use the display's exact colours. Preserve those
// indices without introducing dither, and avoid RGB arithmetic per pixel on
// the 8088. Ordinary GIF palettes retain the existing colour-dither path.
void GifDecoder::GeneratePaletteLUT(int count)
{
	static const uint8_t rgbi[16][3] =
	{
		{0,0,0}, {0,0,170}, {0,170,0}, {0,170,170},
		{170,0,0}, {170,0,170}, {170,85,0}, {170,170,170},
		{85,85,85}, {85,85,255}, {85,255,85}, {85,255,255},
		{255,85,85}, {255,85,255}, {255,255,85}, {255,255,255}
	};
	activePaletteSize = count;
	useTandyPalette = outputImage->bpp == 8 &&
		Platform::video->drawSurface->format == DrawSurface::Format_4BPP_Packed;
	for (int n = 0; n < count; n++)
	{
		uint8_t* rgb = palette + n * 3;
		if (outputImage->bpp == 8)
		{
			paletteLUT[n] = Platform::video->paletteLUT[RGB332(rgb[0], rgb[1], rgb[2])];
            if (Platform::video->drawSurface->format == DrawSurface::Format_4BPP_Packed)
                tandyDitherPairs[n] = TandyRGBDitherPair(rgb[0], rgb[1], rgb[2]);
			if (Platform::video->drawSurface->format == DrawSurface::Format_4BPP_Packed &&
				n != transparentColourIndex)
			{
				int colour;
				for (colour = 0; colour < 16; colour++)
					if (rgb[0] == rgbi[colour][0] && rgb[1] == rgbi[colour][1] &&
						rgb[2] == rgbi[colour][2]) break;
				if (colour == 16) useTandyPalette = false;
				else paletteLUT[n] = colour;
			}
		}
		else paletteLUT[n] = RGB_TO_GREY(rgb[0], rgb[1], rgb[2]);
	}
	if (transparentColourIndex >= 0)
		paletteLUT[transparentColourIndex] = TRANSPARENT_COLOUR_VALUE;
}

void GifDecoder::ClearDictionary()
{
	for(dictionaryIndex = 0; dictionaryIndex < (1 << lzwCodeSize); dictionaryIndex++)
	{
		dictionary[dictionaryIndex].byte = (uint8_t) dictionaryIndex;
		dictionary[dictionaryIndex].prev = -1;
	}
	
	dictionaryIndex += 2;
}

// Compute output index of y-th input line, in frame of height h. 
int GifDecoder::CalculateLineIndex(int y)
{
	int p; /* number of lines in current pass */

	p = (header.height - 1) / 8 + 1;
	if (y < p) /* pass 1 */
		return y * 8;
	y -= p;
	p = (header.height - 5) / 8 + 1;
	if (y < p) /* pass 2 */
		return y * 8 + 4;
	y -= p;
	p = (header.height - 3) / 4 + 1;
	if (y < p) /* pass 3 */
		return y * 4 + 2;
	y -= p;
	/* pass 4 */
	return y * 2 + 1;
}

void GifDecoder::ProcessLineBuffer()
{
	int outputY = linesProcessed;
	
	if (imageDescriptor.fields & GIF_INTERLACE_BIT)
	{
		outputY = CalculateLineIndex(linesProcessed);
	}

	if (outputImage->height == header.height)
	{
		EmitLine(outputY);
	}
	else
	{
		int first = outputY * (long)outputImage->height / header.height;
		int last = (outputY + 1) * (long)outputImage->height / header.height;

		for (int y = first; y < last; y++)
		{
			EmitLine(y);
		}

		if (!(imageDescriptor.fields & GIF_INTERLACE_BIT))
		{
			linesDecoded = last;
		}
	}

	linesProcessed++;
}

void GifDecoder::EmitLine(int y)
{
	MemBlockHandle* lines = outputImage->lines.Get<MemBlockHandle*>();
	MemBlockHandle lineOutput = lines[y];
	uint8_t* output = lineOutput.Get<uint8_t*>();
	
	if (outputImage->bpp == 8)
	{
        if (!useTandyPalette && Platform::video->drawSurface->format == DrawSurface::Format_4BPP_Packed)
        {
            const uint8_t* phases = tandyDitherOrder + ((y & 3) << 2);
            // Use exactly the existing nearest-sample resize accumulator.
            int dy = lineBufferSize << 1, dx = outputImage->width << 1;
            int error = dy - outputImage->width, sourceX = 0;
            for (int x = 0; x < outputImage->width; ++x)
            {
                uint8_t index = lineBuffer[sourceX];
                output[x] = index == transparentColourIndex ? TRANSPARENT_COLOUR_VALUE :
                    TandyDitherColour(tandyDitherPairs[index], phases[x & 3]);
                while (error > 0) { ++sourceX; error -= dx; }
                error += dy;
            }
            lineOutput.Commit();
            return;
        }
		bool useColourDithering = !useTandyPalette;
		if (Platform::video->paletteLUT == compositeCgaPaletteLUT)
		{
			//useColourDithering = false;
		}

		if (useColourDithering)
		{
			const int8_t* ditherPattern = colourDitherMatrix + 4 * (y & 3);
			int ditherIndex = 0;

			if (outputImage->width == lineBufferSize)
			{
				for (int i = 0; i < lineBufferSize; i++)
				{
					int8_t offset = ditherPattern[ditherIndex];
					ditherIndex = (ditherIndex + 1) & 3;

					if (lineBuffer[i] == transparentColourIndex)
					{
						output[i] = TRANSPARENT_COLOUR_VALUE;
						continue;
					}

					int index = lineBuffer[i] * 3;
					int red = palette[index] + offset;
					if (red > 255)
						red = 255;
					else if (red < 0)
						red = 0;
					int green = palette[index + 1] + offset;
					if (green > 255)
						green = 255;
					else if (green < 0)
						green = 0;
					int blue = palette[index + 2] + offset;
					if (blue > 255)
						blue = 255;
					else if (blue < 0)
						blue = 0;

					output[i] = Platform::video->paletteLUT[RGB332(red, green, blue)];
				}
			}
			else
			{
				int dy = lineBufferSize << 1;
				int dx = outputImage->width << 1;
				int D = dy - outputImage->width;
				int x = 0;

				for (int i = 0; i < outputImage->width; i++)
				{
					int offset = ditherPattern[ditherIndex];
					ditherIndex = (ditherIndex + 1) & 3;

					if (lineBuffer[x] == transparentColourIndex)
					{
						output[i] = TRANSPARENT_COLOUR_VALUE;
					}
					else
					{
						int index = lineBuffer[x] * 3;
						int red = palette[index] + offset;
						if (red > 255)
							red = 255;
						else if (red < 0)
							red = 0;
						int green = palette[index + 1] + offset;
						if (green > 255)
							green = 255;
						else if (green < 0)
							green = 0;
						int blue = palette[index + 2] + offset;
						if (blue > 255)
							blue = 255;
						else if (blue < 0)
							blue = 0;

						output[i] = Platform::video->paletteLUT[RGB332(red, green, blue)];
					}

					while (D > 0)
					{
						x++;
						D -= dx;
					}
					D += dy;
				}
			}

		}
		else
		{
			if (outputImage->width == lineBufferSize)
			{
				for (int i = 0; i < lineBufferSize; i++)
				{
					output[i] = paletteLUT[lineBuffer[i]];
				}
			}
			else
			{
				int dy = lineBufferSize << 1;
				int dx = outputImage->width << 1;
				int D = dy - outputImage->width;
				int x = 0;

				for (int i = 0; i < outputImage->width; i++)
				{
					output[i] = paletteLUT[lineBuffer[x]];
					while (D > 0)
					{
						x++;
						D -= dx;
					}
					D += dy;
				}
			}
		}
	}
	else
	{
		const uint8_t* ditherPattern = greyDitherMatrix + 16 * (y & 15);
		uint8_t buffer = 0;
		uint8_t mask = 0x80;
		int ditherIndex = 0;

		if (outputImage->width == lineBufferSize)
		{
			for (int i = 0; i < lineBufferSize; i++)
			{
				uint8_t value = paletteLUT[lineBuffer[i]];
				uint8_t threshold = ditherPattern[ditherIndex];

				if (value > threshold)
				{
					buffer |= mask;
				}

				ditherIndex = (ditherIndex + 1) & 15;

				mask >>= 1;
				if (!mask)
				{
					*output++ = buffer;
					buffer = 0;
					mask = 0x80;
				}
			}

			if (mask != 0x80)
			{
				*output = buffer;
			}
		}
		else
		{
			
			int dy = lineBufferSize << 1;
			int dx = outputImage->width << 1;
			int D = dy - outputImage->width;
			int x = 0;

			for (int i = 0; i < outputImage->width; i++)
			{
				uint8_t value = paletteLUT[lineBuffer[x]];
				uint8_t threshold = ditherPattern[ditherIndex];

				if (value > threshold)
				{
					buffer |= mask;
				}

				ditherIndex = (ditherIndex + 1) & 15;

				mask >>= 1;
				if (!mask)
				{
					*output++ = buffer;
					buffer = 0;
					mask = 0x80;
				}

				while (D > 0)
				{
					x++;
					D -= dx;
				}
				D += dy;
			}
			
			if (mask != 0x80)
			{
				*output = buffer;
			}
		}
	}

	lineOutput.Commit();
}

