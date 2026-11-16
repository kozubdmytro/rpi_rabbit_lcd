#pragma once
#include "gesture_sample.h"
#include "framebuffer.h"
#include <algorithm>
#include <array>
#include <vector>
namespace sr {
inline constexpr int stripX=465,stripWidth=9,stripTop=12,stripBottom=307;
inline void drawTouchStrip(std::vector<RGB>& pixels,int width,int height,int percent=100) {
    if(width!=480||height!=320)return;
    for(int start:{stripX,480-stripX-stripWidth})
        for(int y=stripTop;y<=stripBottom;++y)for(int x=start;x<start+stripWidth;++x)
            pixels[size_t(y)*width+x]={uint8_t(150*percent/100),uint8_t(150*percent/100),uint8_t(150*percent/100)};
}
// Capture a contact on either side until release, just like a regular slider.
class StripContact {
    bool wasDown=false,accepted=false;
    std::array<double,3> recent{};
    unsigned samples=0;
public:
    GestureSample update(double x,double y,bool down) {
        if(!down) {wasDown=accepted=false;samples=0;return {};}
        if(!wasDown) {
            accepted=((x>=0&&x<80)||(x>=400&&x<=479))&&y>=0&&y<=319;
            samples=0;
        }
        wasDown=true;
        if(!accepted)return {};
        // Horizontal drift cannot drop a drag. A three-sample median rejects
        // isolated resistive-touch spikes without quantizing the slider.
        recent[samples%recent.size()]=std::clamp(y,double(stripTop),double(stripBottom));
        ++samples;
        auto sorted=recent;
        const unsigned count=std::min(samples,unsigned(recent.size()));
        std::sort(sorted.begin(),sorted.begin()+count);
        const double position=count==2?(sorted[0]+sorted[1])/2:sorted[count/2];
        return {true,(position-stripTop)/(stripBottom-stripTop)};
    }
};
}
