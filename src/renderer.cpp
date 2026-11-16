#include "renderer.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace sr {
// Headless output has no physical brightness; LCD overrides this method.
void DisplayBackend::setBrightness(int) {}
Viewport Viewport::fit(int w,int h) {
    int scale=std::min(w/64,h/64);
    if(scale<1||w>16384||h>16384) throw std::runtime_error("Display must fit at least 64x64 and be at most 16384x16384");
    return {w,h,scale,(w-64*scale)/2,(h-64*scale)/2};
}
std::optional<std::pair<int,int>> Viewport::logical(int px,int py) const {
    if(px<x||py<y||px>=x+64*scale||py>=y+64*scale) return {};
    return std::pair<int,int>{(px-x)/scale,(py-y)/scale};
}
std::vector<RGB> scaleFrame(const Framebuffer& f,const Viewport& v) {
    std::vector<RGB> out(size_t(v.width)*v.height);
    for(int y=0;y<64*v.scale;++y) for(int x=0;x<64*v.scale;++x)
        out[size_t(y+v.y)*v.width+x+v.x]=f.at(x/v.scale,y/v.scale);
    return out;
}
void writePPM(const std::string& path,int w,int h,const RGB* data) {
    std::ofstream file(path,std::ios::binary);
    file<<"P6\n"<<w<<' '<<h<<"\n255\n";
    file.write(reinterpret_cast<const char*>(data),std::streamsize(w)*h*3);
    if(!file) throw std::runtime_error("Cannot write image: "+path);
}
class Headless final:public DisplayBackend {
    Viewport v;
public:
    Headless(int w,int h):v(Viewport::fit(w,h)){}
    void present(const Framebuffer&) override {}
    const Viewport& viewport() const override { return v; }
    std::string name() const override { return "headless (NO physical display output)"; }
};
std::unique_ptr<DisplayBackend> createHeadless(int w,int h) {return std::make_unique<Headless>(w,h);}
}
