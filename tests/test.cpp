#include "framebuffer.h"
#include "renderer.h"
#include "lcd_strip.h"
#include "hub_demo.h"
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
        auto withStrip=out;drawTouchStrip(withStrip,480,320);
        for(int y=0;y<320;++y)for(int x=0;x<480;++x) {
            const bool line=((x>=6&&x<15)||(x>=465&&x<474))&&y>=12&&y<=307;
            check(equal(withStrip[y*480+x],line?RGB{150,150,150}:out[y*480+x]),"only the two thicker strips change the output");
        }
        StripContact strip;
        check(strip.update(468,12,true).position==0,"strip top is zero");
        check(strip.update(468,307,true).position==1,"downward swipe increases position");
        check(strip.update(468,160,false).position==-1,"release");
        check(strip.update(399,160,true).position==-1,"UI touch ignored");
        check(strip.update(468,160,true).position==-1,"entering strip during outside contact ignored");
        strip.update(468,160,false);
        check(strip.update(450,160,true).position>0,"wide hit area beside line");
        check(!strip.update(430,160,true).healthy,"leaving strip cancels without a click");
        check(!strip.update(468,160,true).healthy,"cancelled contact cannot resume");
        check(strip.update(468,160,false).healthy,"release clears cancellation");
        check(strip.update(10,12,true).position==0,"left strip top is zero");
        check(strip.update(10,307,true).position==1,"left strip downward swipe increases position");
        check(!strip.update(468,307,true).healthy,"switching sides during contact cancels");
        strip.update(10,307,false);
        check(strip.update(40,160,true).position==-1,"left hit area stops before UI");
        check(strip.update(10,160,true).position==-1,"outside contact cannot enter left strip");
        strip.update(10,160,false);
        drawTouchStrip(withStrip,480,320,0);
        check(equal(withStrip[160*480+468],black)&&equal(withStrip[160*480+10],black),"zero brightness hides line while touch stays available");
        for(int controlX:{10,468}) {
            HubDemo ui;mesh::View state;Framebuffer uiFrame;StripContact control;
            auto gesture=[&](double y,bool down,int64_t time) {ui.draw(uiFrame,control.update(controlX,y,down),time,state);};
            gesture(240,true,1000000);gesture(40,true,1100000);gesture(40,false,1200000); // Quick.
            gesture(180,true,1300000);gesture(180,true,2010000); // Hold starts brightness adjustment.
            gesture(307,true,2050000);
            check(ui.displayBrightness(50)==100,"strip hold and motion reach maximum brightness");
            gesture(307,false,2100000);
            check(ui.takeBrightnessRequest()==100,"release saves brightness through existing UI contract");
        }
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
        std::cout<<"PASS: native RGB, scaling, margins, coordinates, touch strip and all rotations\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
