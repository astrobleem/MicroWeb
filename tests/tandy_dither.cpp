#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include "../src/Colour.h"
static const int rgb[16][3] = {
    {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
    {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}};
static const int weight[3] = {2,4,1};
static int64_t score(const int* target,int first,int second,int count)
{
    int64_t result=0;
    for(int c=0;c<3;++c)
    {
        int64_t delta=rgb[second][c]-rgb[first][c];
        int64_t error=16*target[c]-16*rgb[first][c]-count*delta;
        result+=weight[c]*(20*error*error+delta*delta*count*(16-count));
    }
    return result;
}
int main()
{
    unsigned phases=0;
    for(int i=0;i<16;++i) phases|=1u<<tandyDitherOrder[i];
    assert(phases==65535);
    double oldError=0,newError=0;
    for(int r=0;r<16;++r) for(int g=0;g<16;++g) for(int b=0;b<16;++b)
    {
        int target[3]={r*17,g*17,b*17};
        uint16_t pair=TandyRGBDitherPair(target[0],target[1],target[2]);
        int first=pair&15,second=(pair>>4)&15,count=pair>>8;
        assert(count<16);
        int64_t actual=score(target,first,second,count),best=LLONG_MAX;
        // Brute-force every possible pair and coverage, independent of the
        // generator's projection/derivative shortcut.
        for(int a=0;a<16;++a) for(int z=a;z<16;++z) for(int n=0;n<=16;++n)
        {
            int64_t value=score(target,a,z,n);
            if(value<best) best=value;
        }
        assert(actual==best);
        int oldSum[3]={0,0,0},newSum[3]={0,0,0};
        for(int phase=0;phase<16;++phase)
        {
            int colour=TandyDitherColour(pair,phase);assert(colour<16);
            int adjusted[3];
            for(int c=0;c<3;++c)
            { int v=target[c]+phase*4-32;adjusted[c]=v<0?0:v>255?255:v; }
            int old=egaPaletteLUT[RGB332(adjusted[0],adjusted[1],adjusted[2])];
            for(int c=0;c<3;++c) {oldSum[c]+=rgb[old][c];newSum[c]+=rgb[colour][c];}
        }
        for(int c=0;c<3;++c)
        { double a=target[c]-oldSum[c]/16.0,z=target[c]-newSum[c]/16.0;
          oldError+=weight[c]*a*a;newError+=weight[c]*z*z; }
    }
    for(int c=0;c<16;++c) for(int phase=0;phase<16;++phase)
        assert(TandyDitherColour(TandyRGBDitherPair(rgb[c][0],rgb[c][1],rgb[c][2]),phase)==c);
    for(int y=0;y<256;++y) for(int phase=0;phase<16;++phase)
    {
        int colour=TandyDitherColour(TandyYCCDitherPair(y,128,128),phase);
        assert(colour==0 || colour==7 || colour==8 || colour==15);
    }
    assert(newError<oldError*.5);
    printf("PASS: 4096 exhaustive pair optimizations; exact RGBI; neutral JPEG; palette bounds; "
           "weighted 4x4 mean error %.0f -> %.0f (%.1f%% lower)\n",oldError,newError,100*(1-newError/oldError));
}
