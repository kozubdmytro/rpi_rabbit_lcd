#include "renderer.h"
#include "lcd_strip.h"
#include <array>
#include <stdexcept>
#ifdef SR_HAVE_DRM
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_mode.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <linux/kd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace sr {
namespace {
class Drm final:public DisplayBackend {
    int fd=-1,tty=-1,oldMode=KD_TEXT,percent=50;
    bool consoleOwned=false,modeSet=false,first=true;
    uint32_t connector=0,crtc=0,fb=0,handle=0,pitch=0;
    uint64_t mapSize=0;
    uint8_t* memory=nullptr;
    drmModeCrtc* saved=nullptr;
    Viewport view{0,0,0,0,0};
    std::array<uint32_t,64*64> previous{};
    std::chrono::steady_clock::time_point lastFullRefresh{};
    std::string previousGovernor;
    bool performanceLeased=false;
    static constexpr const char* governorPath="/sys/devices/system/cpu/cpufreq/policy0/scaling_governor";
    static constexpr const char* governorState="/run/smartrabbit-test/previous-governor";
    std::string path;
    [[noreturn]] void fail(const std::string& message) const {throw std::runtime_error(message+": "+std::strerror(errno));}
    static uint16_t packed(RGB c) {return uint16_t(((unsigned(c.r)*31+127)/255)<<11|((unsigned(c.g)*63+127)/255)<<5|((unsigned(c.b)*31+127)/255));}
    void acquirePerformance() {
        std::ifstream current(governorPath);std::getline(current,previousGovernor);
        if(previousGovernor.empty())throw std::runtime_error("Cannot read CPU governor for stable SPI output");
        // A recovery file survives a killed manual instance until restoration.
        std::ifstream recovery(governorState);std::string savedPolicy;std::getline(recovery,savedPolicy);
        if(!savedPolicy.empty())previousGovernor=savedPolicy;
        std::filesystem::create_directories("/run/smartrabbit-test");
        {std::ofstream state(governorState);state<<previousGovernor<<'\n';if(!state)throw std::runtime_error("Cannot save display performance policy");}
        performanceLeased=true;
        std::ofstream policy(governorPath);policy<<"performance"<<std::flush;
        if(!policy)throw std::runtime_error("Cannot select stock performance governor");
    }
    void cleanup() noexcept {
        if(modeSet) {
            if(saved&&saved->mode_valid)drmModeSetCrtc(fd,saved->crtc_id,saved->buffer_id,saved->x,saved->y,&connector,1,&saved->mode);
            else drmModeSetCrtc(fd,crtc,0,0,0,nullptr,0,nullptr);
        }
        if(saved){drmModeFreeCrtc(saved);saved=nullptr;}
        if(fb){drmModeRmFB(fd,fb);fb=0;}
        if(memory){munmap(memory,mapSize);memory=nullptr;}
        if(handle){drm_mode_destroy_dumb destroy{};destroy.handle=handle;drmIoctl(fd,DRM_IOCTL_MODE_DESTROY_DUMB,&destroy);handle=0;}
        if(consoleOwned)ioctl(tty,KDSETMODE,oldMode);
        if(tty>=0){close(tty);tty=-1;}
        if(fd>=0){drmDropMaster(fd);close(fd);fd=-1;}
        if(performanceLeased) {
            std::ofstream policy(governorPath);policy<<previousGovernor<<std::flush;
            if(policy){std::error_code error;std::filesystem::remove(governorState,error);}
            performanceLeased=false;
        }
    }
public:
    explicit Drm(const std::string& device) {
        try {
            std::vector<std::string> candidates;
            if(!device.empty())candidates.push_back(device);
            else for(const auto& e:std::filesystem::directory_iterator("/dev/dri"))if(e.path().filename().string().rfind("card",0)==0)candidates.push_back(e.path().string());
            for(const auto& candidate:candidates) {
                int probe=open(candidate.c_str(),O_RDWR|O_CLOEXEC);if(probe<0)continue;
                drmVersionPtr version=drmGetVersion(probe);
                bool match=version&&std::string(version->name,version->name_len)=="ili9486";
                if(version)drmFreeVersion(version);
                if(match){fd=probe;path=candidate;break;}close(probe);
            }
            if(fd<0)throw std::runtime_error("No native ili9486 DRM device found. Enable the tested display overlay first");
            if(flock(fd,LOCK_EX|LOCK_NB)<0)fail("DRM display already in use");
            if(drmSetMaster(fd)<0)fail("Cannot acquire DRM master");
            drmModeRes* resources=drmModeGetResources(fd);
            if(!resources)fail("Cannot query DRM resources");
            drmModeModeInfo mode{};
            for(int i=0;i<resources->count_connectors&&!connector;++i) {
                auto* c=drmModeGetConnector(fd,resources->connectors[i]);
                if(c&&c->connection==DRM_MODE_CONNECTED&&c->count_modes>0) {
                    mode=c->modes[0];connector=c->connector_id;
                    auto* encoder=c->encoder_id?drmModeGetEncoder(fd,c->encoder_id):nullptr;
                    if(encoder){crtc=encoder->crtc_id;drmModeFreeEncoder(encoder);}
                    if(!crtc&&resources->count_crtcs>0)crtc=resources->crtcs[0];
                }
                if(c)drmModeFreeConnector(c);
            }
            drmModeFreeResources(resources);
            if(!connector||!crtc)throw std::runtime_error("No connected SPI connector/CRTC");
            if(mode.hdisplay!=480||mode.vdisplay!=320)throw std::runtime_error("LCD DRM mode must be 480x320");
            view=Viewport::fit(mode.hdisplay,mode.vdisplay);saved=drmModeGetCrtc(fd,crtc);
            if(!saved)fail("Cannot save DRM console state");
            tty=open("/dev/tty0",O_RDWR|O_CLOEXEC);
            if(tty<0||ioctl(tty,KDGETMODE,&oldMode)<0)fail("Console access required; use ./run.sh");
            if(oldMode!=KD_TEXT)throw std::runtime_error("Console already in graphics mode; stop the running UI first");
            if(ioctl(tty,KDSETMODE,KD_GRAPHICS)<0)fail("Cannot acquire graphics console");
            consoleOwned=true;
            acquirePerformance();
            drm_mode_create_dumb create{};create.width=view.width;create.height=view.height;create.bpp=16;
            if(drmIoctl(fd,DRM_IOCTL_MODE_CREATE_DUMB,&create)<0)fail("Cannot allocate DRM RGB565 buffer");
            handle=create.handle;pitch=create.pitch;mapSize=create.size;
            if(pitch<uint32_t(view.width*2)||uint64_t(pitch)*view.height>mapSize)throw std::runtime_error("Invalid DRM buffer geometry");
            if(drmModeAddFB(fd,view.width,view.height,16,16,pitch,handle,&fb)<0)fail("Cannot register DRM framebuffer");
            drm_mode_map_dumb map{};map.handle=handle;
            if(drmIoctl(fd,DRM_IOCTL_MODE_MAP_DUMB,&map)<0)fail("Cannot map DRM framebuffer");
            void* ptr=mmap(nullptr,mapSize,PROT_READ|PROT_WRITE,MAP_SHARED,fd,map.offset);
            if(ptr==MAP_FAILED)fail("Cannot mmap DRM framebuffer");
            memory=static_cast<uint8_t*>(ptr);std::memset(memory,0,mapSize);
            if(drmModeSetCrtc(fd,crtc,fb,0,0,&connector,1,&mode)<0)fail("Cannot activate SPI scanout");
            modeSet=true;
        } catch(...) {cleanup();throw;}
    }
    ~Drm() override {cleanup();}
    void setBrightness(int value) override {percent=std::clamp(value,0,100);}
    RGB outputColor(RGB color) const {
        return {uint8_t(color.r*percent/100),uint8_t(color.g*percent/100),uint8_t(color.b*percent/100)};
    }
    void present(const Framebuffer& frame) override {
        int x0=view.width,y0=view.height,x1=-1,y1=-1;
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
            auto rgb=outputColor(frame.at(x,y));auto color=packed(rgb);int index=y*64+x;
            uint32_t key=uint32_t(rgb.r)<<16|uint32_t(rgb.g)<<8|rgb.b;
            if(!first&&previous[index]==key)continue;
            previous[index]=key;
            x0=std::min(x0,view.x+x*view.scale);x1=std::max(x1,view.x+(x+1)*view.scale-1);
            y0=std::min(y0,view.y+y*view.scale);y1=std::max(y1,view.y+(y+1)*view.scale-1);
            for(int dy=0;dy<view.scale;++dy) {
                auto* row=reinterpret_cast<uint16_t*>(memory+size_t(view.y+y*view.scale+dy)*pitch);
                std::fill_n(row+view.x+x*view.scale,view.scale,color);
            }
        }
        // The physical touch strips stay outside the logical UI. They only need
        // another upload when their color changes, for example with brightness.
        const uint16_t stripColor=packed(outputColor({150,150,150}));
        for(int start:{stripX,480-stripX-stripWidth})for(int y=stripTop;y<=stripBottom;++y) {
            auto* row=reinterpret_cast<uint16_t*>(memory+size_t(y)*pitch);
            for(int x=start;x<start+stripWidth;++x)if(first||row[x]!=stripColor) {
                row[x]=stripColor;x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);
            }
        }
        // Clear the entire physical panel on first use, including side margins.
        // Periodic full uploads repair any stale LCD pixels after a transient
        // transfer problem; the UI and its animation remain strictly 64x64.
        auto now=std::chrono::steady_clock::now();
        bool full=first||now-lastFullRefresh>=std::chrono::seconds(2);
        first=false;if(x1<0&&!full)return;
        drmModeClip clip{uint16_t(x0),uint16_t(y0),uint16_t(x1+1),uint16_t(y1+1)};
        if(full)clip={0,0,uint16_t(view.width),uint16_t(view.height)};
        // ILI9486's blocking DIRTYFB path returns after the SPI upload. Unlike
        // fbdev deferred I/O, only this rectangle (including X bounds) is sent.
        int result;
        do {result=drmModeDirtyFB(fd,fb,&clip,1);} while(result<0&&errno==EINTR);
        if(result<0)fail("DRM rectangular upload failed");
        if(full)lastFullRefresh=now;
    }
    const Viewport& viewport() const override {return view;}
    std::string name() const override {return "native ILI9486 DRM / "+path+" / RGB565 / synchronous rectangular updates";}
    void verifyOutput(const Framebuffer& frame) const override {
        auto* active=drmModeGetCrtc(fd,crtc);
        bool correct=active&&active->buffer_id==fb&&active->mode.hdisplay==view.width&&active->mode.vdisplay==view.height;
        if(active)drmModeFreeCrtc(active);
        if(!correct)throw std::runtime_error("Active DRM scanout does not match the UI buffer");
        for(int y=0;y<view.height;++y)for(int x=0;x<view.width;++x) {
            auto logical=view.logical(x,y);uint16_t expected=logical?packed(outputColor(frame.at(logical->first,logical->second))):0;
            if(((x>=stripX&&x<stripX+stripWidth)||(x>=480-stripX-stripWidth&&x<480-stripX))&&y>=stripTop&&y<=stripBottom)
                expected=packed(outputColor({150,150,150}));
            auto* row=reinterpret_cast<const uint16_t*>(memory+size_t(y)*pitch);
            if(row[x]!=expected)throw std::runtime_error("DRM scanout pixel mismatch at "+std::to_string(x)+","+std::to_string(y));
        }
    }
};
}
std::unique_ptr<DisplayBackend> createDrmDisplay(const std::string& device) {return std::make_unique<Drm>(device);}
}
#else
namespace sr {std::unique_ptr<DisplayBackend> createDrmDisplay(const std::string&) {throw std::runtime_error("Native DRM support requires libdrm-dev and a rebuild");}}
#endif
