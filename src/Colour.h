#ifndef _COLOUR_H_
#define _COLOUR_H_

#include <stdint.h>

#define RGB332(red, green, blue) (((red) & 0xe0) | (((green) & 0xe0) >> 3) | (((blue) & 0xc0) >> 6))
#define RGB_TO_GREY(red, green, blue) (((red) * 76 + (green) * 150 + (blue) * 30) >> 8)
#define RGB666(red, green, blue) ( ( ( (red) * 5) / 255) * 36) + ( ( ( (green) * 5) / 255) * 6) + ( ( ( (blue) * 5) / 255) ) + 16 

#define TRANSPARENT_COLOUR_VALUE 0xff

struct ColourScheme
{
	uint8_t pageColour;
	uint8_t textColour;
	uint8_t linkColour;
	uint8_t buttonColour;
};

struct NamedColour
{
	const char* name;
	uint8_t colour;
};

extern ColourScheme monochromeColourScheme;
extern ColourScheme egaColourScheme;
extern ColourScheme cgaColourScheme;
extern ColourScheme compositeCgaColourScheme;
extern ColourScheme colourScheme666;

extern uint8_t cgaPaletteLUT[];
extern uint8_t egaPaletteLUT[];
extern uint8_t compositeCgaPaletteLUT[];
extern uint8_t YCbCrToRGB[];

// RGBI-specific ordered dithering. Call only for packed Tandy RGBI modes;
// other modes retain their existing colour lookup and monochrome paths.
extern const uint16_t tandyRGBDitherPairs[4096];
extern const uint16_t tandyYCCDitherPairs[4096];
inline uint16_t TandyRGBDitherPair(uint8_t red, uint8_t green, uint8_t blue)
{
    unsigned index = ((unsigned)(red & 0xf0) << 4) | (green & 0xf0) | (blue >> 4);
    return tandyRGBDitherPairs[index];
}
inline uint16_t TandyYCCDitherPair(uint8_t y, uint8_t cb, uint8_t cr)
{
    unsigned index = ((unsigned)(y & 0xf0) << 4) | (cb & 0xf0) | (cr >> 4);
    return tandyYCCDitherPairs[index];
}
extern const uint8_t tandyDitherOrder[16];
inline uint8_t TandyDitherColour(uint16_t pair, unsigned phase)
{
    return (uint8_t)(phase < (pair >> 8) ? (pair >> 4) & 15 : pair & 15);
}

#endif
