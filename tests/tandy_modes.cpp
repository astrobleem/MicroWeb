#include <assert.h>
#include <stdio.h>
#include "../src/VidModes.h"

int main()
{
	assert(GetNumVideoModes() == 21);
	assert(VideoModeList[0].biosVideoMode == 6);
	assert(VideoModeList[18].biosVideoMode == 0x0105);
	VideoModeInfo& tandy = VideoModeList['t' - 'a'];
	assert(tandy.biosVideoMode == 9);
	assert(tandy.screenWidth == 320 && tandy.screenHeight == 200);
	assert(tandy.surfaceFormat == DrawSurface::Format_4BPP_Packed);
	assert(tandy.dataPackIndex == DataPack::Lowres);
	assert(!tandy.useMouseDriverCursor);
	assert(tandy.vramPage1 == 0xb800 && tandy.vramPage2 == 0xba00);
	assert(tandy.vramPage3 == 0xbc00 && tandy.vramPage4 == 0xbe00);
	VideoModeInfo& tga2 = VideoModeList['u' - 'a'];
	assert(tga2.biosVideoMode == TGA2_MODE);
	assert(tga2.screenWidth == 640 && tga2.screenHeight == 200);
	assert(tga2.surfaceFormat == DrawSurface::Format_4BPP_Packed);
	assert(tga2.dataPackIndex == DataPack::CGA);
	assert(!tga2.useMouseDriverCursor);
	assert(tga2.vramPage1 == 0xa000 && !tga2.vramPage2 && !tga2.vramPage3 && !tga2.vramPage4);
	assert(VideoModeList[GetNumVideoModes()].name == NULL);
	puts("PASS: Tandy t, TGA-II u linear640x200 RGBI; existing mode letters preserved; sentinel excluded");
	return 0;
}
