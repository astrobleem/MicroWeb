#ifndef IMAGE_TEST_SURFACE_H
#define IMAGE_TEST_SURFACE_H
#include "../src/Draw/Surface.h"
// No display hardware is involved. Only the format selects the decoder path.
class ImageTestSurface : public DrawSurface
{
public:
    ImageTestSurface(Format f) : DrawSurface(320,200) { format=f; lines=NULL; }
    void Clear() {}
    void HLine(DrawContext&,int,int,int,uint8_t) {}
    void VLine(DrawContext&,int,int,int,uint8_t) {}
    void FillRect(DrawContext&,int,int,int,int,uint8_t) {}
    void InvertRect(DrawContext&,int,int,int,int) {}
    void DrawString(DrawContext&,Font*,const char*,int,int,uint8_t,FontStyle::Type) {}
    void BlitImage(DrawContext&,Image*,int,int) {}
    void VerticalScrollBar(DrawContext&,int,int,int,int,int) {}
};
#endif
