#include "framebuffer.h"
#include <algorithm>
#include <cstdlib>
#include "dina_font.h"

namespace sr {
// Retain the existing small digits and custom UI symbol placeholders.
static std::array<uint8_t,5> glyph(char ch) {
    switch (ch) {
    case '0': return {7,5,5,5,7}; case '1': return {2,6,2,2,7};
    case '2': return {6,1,2,4,7}; case '3': return {6,1,2,1,6};
    case '4': return {5,5,7,1,1}; case '5': return {7,4,6,1,6};
    case '6': return {3,4,7,5,7}; case '7': return {7,1,2,2,2};
    case '8': return {7,5,7,5,7}; case '9': return {7,5,7,1,6};
    case '%': return {5,1,2,4,5};
    case '^': return {7,5,7,0,0};
    default: return {};
    }
}
void Framebuffer::clear(RGB c) { pixels.fill(c); }
void Framebuffer::pixel(int x,int y,RGB c) { if(x>=0&&y>=0&&x<width&&y<height) pixels[y*width+x]=c; }
RGB Framebuffer::at(int x,int y) const { return pixels.at(y*width+x); }
void Framebuffer::rect(int x,int y,int w,int h,RGB c) { for(int j=0;j<h;++j) for(int i=0;i<w;++i) pixel(x+i,y+j,c); }
void Framebuffer::line(int x,int y,int xx,int yy,RGB c) {
    int dx=std::abs(xx-x), sx=x<xx?1:-1, dy=-std::abs(yy-y), sy=y<yy?1:-1, e=dx+dy;
    for(;;) { pixel(x,y,c); if(x==xx&&y==yy) break; int ee=2*e; if(ee>=dy){e+=dy;x+=sx;} if(ee<=dx){e+=dx;y+=sy;} }
}
static bool retainedGlyph(char ch) { return (ch>='0'&&ch<='9')||ch=='%'||ch=='^'; }
void Framebuffer::text(int x,int y,std::string_view s,RGB c,int scale) {
    for(unsigned char ch:s) {
        if(retainedGlyph(ch)) {
            auto g=glyph(ch);
            for(int j=0;j<5;++j)for(int i=0;i<3;++i)
                if(g[j]&(1<<(2-i)))rect(x+i*scale,y+j*scale,scale,scale,c);
            x+=4*scale;
        } else {
            for(int j=0;j<16;++j)for(int i=0;i<8;++i)
                if(dinaFont[ch][j]&(128>>i))rect(x+i*scale,y+j*scale,scale,scale,c);
            x+=dinaAdvance(ch)*scale;
        }
    }
}
int Framebuffer::textWidth(std::string_view s,int scale) {
    int width=0;
    for(unsigned char ch:s)width+=retainedGlyph(ch)?4:dinaAdvance(ch);
    return s.empty()?0:(width-1)*scale;
}
void Framebuffer::centered(int y,std::string_view s,RGB c,int scale) { text((width-textWidth(s,scale))/2,y,s,c,scale); }
RGB brightness(RGB c,int level) { level=std::clamp(level,0,255); return {uint8_t(c.r*level/255),uint8_t(c.g*level/255),uint8_t(c.b*level/255)}; }
}
