#include "renderer.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace sr {
namespace {
class LcdDisplay final:public DisplayBackend {
    int fd=-1,percent=50;
    fb_fix_screeninfo fixed{};
    fb_var_screeninfo screen{};
    uint8_t* memory=nullptr;
    Viewport v=Viewport::fit(480,320);
    void release() {
        if(memory)munmap(memory,fixed.smem_len);
        if(fd>=0)close(fd);
    }
    uint32_t channel(uint8_t value,const fb_bitfield& field) const {
        if(!field.length)return 0;
        const uint32_t maximum=(uint32_t(1)<<field.length)-1;
        return ((uint32_t(value)*maximum+127)/255)<<field.offset;
    }
public:
    explicit LcdDisplay(const std::string& device) {
        try {
            fd=open(device.c_str(),O_RDWR|O_CLOEXEC);
            if(fd<0)throw std::runtime_error("Cannot open LCD framebuffer: "+device);
            if(ioctl(fd,FBIOGET_FSCREENINFO,&fixed)<0||ioctl(fd,FBIOGET_VSCREENINFO,&screen)<0)
                throw std::runtime_error("Cannot query LCD framebuffer: "+device);
            if(screen.xres!=480||screen.yres!=320)
                throw std::runtime_error("LCD framebuffer must be 480x320; refusing to resize or stretch UI");
            if(fixed.type!=FB_TYPE_PACKED_PIXELS||fixed.visual!=FB_VISUAL_TRUECOLOR||screen.grayscale||
               (screen.bits_per_pixel!=16&&screen.bits_per_pixel!=24&&screen.bits_per_pixel!=32))
                throw std::runtime_error("LCD requires packed truecolor RGB at 16, 24 or 32 bits per pixel");
            // FBTFT fb_ili9486 reports nonstd=1 for its ordinary RGB565 buffer.
            const bool ili9486=std::strncmp(fixed.id,"fb_ili9486",sizeof(fixed.id))==0&&
                screen.bits_per_pixel==16&&screen.red.offset==11&&screen.red.length==5&&
                screen.green.offset==5&&screen.green.length==6&&screen.blue.offset==0&&
                screen.blue.length==5&&!screen.transp.length;
            if(screen.nonstd&&!(screen.nonstd==1&&ili9486))
                throw std::runtime_error("Unsupported nonstandard LCD framebuffer");
            uint32_t used=0;
            for(const auto* field:{&screen.red,&screen.green,&screen.blue,&screen.transp}) {
                if(field->msb_right||field->length>8||field->offset>screen.bits_per_pixel||
                   field->length>screen.bits_per_pixel-field->offset)
                    throw std::runtime_error("Unsupported LCD color layout");
                const uint32_t mask=field->length?((uint32_t(1)<<field->length)-1)<<field->offset:0;
                if(mask&used)throw std::runtime_error("Overlapping LCD color channels");
                used|=mask;
            }
            if(!screen.red.length||!screen.green.length||!screen.blue.length)
                throw std::runtime_error("LCD framebuffer has missing RGB channels");
            const size_t rowEnd=(size_t(screen.xoffset)+480)*(screen.bits_per_pixel/8);
            const size_t end=(size_t(screen.yoffset)+319)*fixed.line_length+rowEnd;
            if(rowEnd>fixed.line_length||end>fixed.smem_len)
                throw std::runtime_error("LCD framebuffer memory does not cover the visible screen");
            void* mapped=mmap(nullptr,fixed.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
            if(mapped==MAP_FAILED)throw std::runtime_error("Cannot map LCD framebuffer: "+device);
            memory=static_cast<uint8_t*>(mapped);
        } catch(...) {release();throw;}
    }
    ~LcdDisplay() override {release();}
    void setBrightness(int value) override {percent=std::clamp(value,0,100);}
    void present(const Framebuffer& frame) override {
        auto pixels=scaleFrame(frame,v);
        const unsigned bytes=screen.bits_per_pixel/8;
        for(int y=0;y<v.height;++y) {
            auto* row=memory+(size_t(y)+screen.yoffset)*fixed.line_length+size_t(screen.xoffset)*bytes;
            for(int x=0;x<v.width;++x) {
                const auto& color=pixels[size_t(y)*v.width+x];
                const uint32_t packed=channel(uint8_t(color.r*percent/100),screen.red)|
                    channel(uint8_t(color.g*percent/100),screen.green)|
                    channel(uint8_t(color.b*percent/100),screen.blue)|channel(255,screen.transp);
                std::memcpy(row+size_t(x)*bytes,&packed,bytes);
            }
        }
    }
    const Viewport& viewport() const override {return v;}
    std::string name() const override {return "LCD 480x320 (64x64 at exact 5x scale)";}
};
}
std::unique_ptr<DisplayBackend> createLcdDisplay(const std::string& device) {
    return std::make_unique<LcdDisplay>(device);
}
}
