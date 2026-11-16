#pragma once
#include "framebuffer.h"
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace sr {
struct Viewport {
    int width,height,scale,x,y;
    static Viewport fit(int width,int height);
    std::optional<std::pair<int,int>> logical(int physicalX,int physicalY) const;
};
// Backends consume the same 64x64 RGB frame.
class DisplayBackend {
public:
    virtual ~DisplayBackend()=default;
    virtual void present(const Framebuffer& frame)=0;
    virtual const Viewport& viewport() const=0;
    virtual std::string name() const=0;
    // Physical backends implement display brightness.
    virtual void setBrightness(int percent);
    virtual void verifyOutput(const Framebuffer& frame) const;
};
std::vector<RGB> scaleFrame(const Framebuffer& f,const Viewport& v);
void writePPM(const std::string& path,int w,int h,const RGB* pixels);
std::unique_ptr<DisplayBackend> createHeadless(int w,int h);
std::unique_ptr<DisplayBackend> createDrmDisplay(const std::string& device);
std::unique_ptr<DisplayBackend> createLcdDisplay(const std::string& device);
}
