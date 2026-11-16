#include "framebuffer.h"
#include "renderer.h"
#include "screens/readings.h"
#include <iostream>
#include <stdexcept>
using namespace sr;
static void check(bool result,const char* what) {if(!result)throw std::runtime_error(what);}
static bool equal(RGB a,RGB b) {return a.r==b.r&&a.g==b.g&&a.b==b.b;}
int main() {
    try {
        static_assert(Framebuffer::width==64&&Framebuffer::height==64);
        static_assert(sizeof(Framebuffer::pixels)==64*64*3);
        auto v=Viewport::fit(480,320);
        check(v.scale==5&&v.x==80&&v.y==0,"exact target viewport");
        check(!v.logical(79,0)&&!v.logical(400,0)&&!v.logical(80,320)&&!v.logical(80,-1),"touch margins ignored");
        check(v.logical(80,0)==std::pair<int,int>{0,0},"first touch pixel");
        check(v.logical(399,319)==std::pair<int,int>{63,63},"last touch pixel");
        check(v.logical(85,5)==std::pair<int,int>{1,1},"5x touch conversion");
        auto portrait=Viewport::fit(320,480);check(portrait.scale==5&&portrait.x==0&&portrait.y==80,"portrait mode reported, not stretched");
        Framebuffer f;
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)f.pixel(x,y,{uint8_t(x*4),uint8_t(y*4),uint8_t((x+y)%2*255)});
        auto native=Viewport::fit(64,64);
        check(native.scale==1&&native.x==0&&native.y==0,"matrix viewport has no scaling or margins");
        auto nativePixels=scaleFrame(f,native);
        for(int i=0;i<64*64;++i)check(equal(nativePixels[i],f.pixels[i]),"64x64 output preserves every RGB pixel");
        auto out=scaleFrame(f,v);
        for(int y=0;y<320;++y)for(int x=0;x<480;++x)check(equal(out[y*480+x],x<80||x>=400?black:f.at((x-80)/5,y/5)),"every physical pixel is an exact sample; margins black");
        for(uint8_t rotation=1;rotation<=4;++rotation) {
            auto rotated=f;screens::rotate(rotated,rotation);
            for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
                int nx=x,ny=y;
                if(rotation==2){nx=63-y;ny=x;}
                else if(rotation==3){nx=63-x;ny=63-y;}
                else if(rotation==4){nx=y;ny=63-x;}
                check(equal(rotated.at(nx,ny),f.at(x,y)),"rotation preserves every RGB pixel");
            }
        }
        std::cout<<"PASS: native RGB, scaling, margins, coordinates and all rotations\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
