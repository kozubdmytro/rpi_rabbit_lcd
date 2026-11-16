#pragma once
#include "gesture_sample.h"
#include "framebuffer.h"
#include <algorithm>
#include <vector>
namespace sr {
inline constexpr int stripX=465,stripWidth=9,stripTop=12,stripBottom=307,stripHitLeft=440;
inline void drawTouchStrip(std::vector<RGB>& pixels,int width,int height,int percent=100) {
    if(width!=480||height!=320)return;
    for(int start:{stripX,480-stripX-stripWidth})
        for(int y=stripTop;y<=stripBottom;++y)for(int x=start;x<start+stripWidth;++x)
            pixels[size_t(y)*width+x]={uint8_t(150*percent/100),uint8_t(150*percent/100),uint8_t(150*percent/100)};
}
// A contact must begin in the strip. Leaving it cancels, never clicks.
class StripContact {
    bool wasDown=false,accepted=false,cancelled=false;
    int captured=-1;
public:
    GestureSample update(double x,double y,bool down) {
        const int side=x>=0&&x<=39?0:(x>=stripHitLeft&&x<=479?1:-1);
        const bool inside=side>=0&&y>=0&&y<=319;
        if(down&&!wasDown) {accepted=inside;captured=side;cancelled=false;}
        if(down&&accepted&&(!inside||side!=captured)) {accepted=false;cancelled=true;}
        wasDown=down;
        if(!down) {accepted=cancelled=false;return {};}
        if(cancelled)return {false,-1};
        return {true,accepted?std::clamp((y-stripTop)/(stripBottom-stripTop),0.0,1.0):-1};
    }
};
}
