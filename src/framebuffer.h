#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace sr {
struct RGB { uint8_t r=0, g=0, b=0; };
static_assert(sizeof(RGB)==3, "Framebuffer must be packed RGB");
inline constexpr RGB black{}, white{255,223,177}, orange{255,142,53}, dim{81,57,37}, blue{81,165,230};
class Framebuffer {
public:
    static constexpr int width=64, height=64;
    std::array<RGB, width*height> pixels{};
    void clear(RGB c=black);
    void pixel(int x,int y,RGB c);
    RGB at(int x,int y) const;
    void rect(int x,int y,int w,int h,RGB c);
    void line(int x,int y,int xx,int yy,RGB c);
    void text(int x,int y,std::string_view s,RGB c=white,int scale=1);
    static int textWidth(std::string_view s,int scale=1);
    void centered(int y,std::string_view s,RGB c=white,int scale=1);
};
RGB brightness(RGB c,int level);
}
