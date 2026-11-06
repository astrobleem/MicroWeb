#include <assert.h>
#include <stdio.h>
#include "../src/VidModes.h"

int main()
{
	assert(GetNumVideoModes() == 20);
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
	assert(VideoModeList[GetNumVideoModes()].name == NULL);
	puts("PASS: Tandy mode t, BIOS09, four banks; existing mode letters preserved; sentinel excluded");
	return 0;
}
