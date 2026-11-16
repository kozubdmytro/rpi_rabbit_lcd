#pragma once
#include "lcd_strip.h"
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
namespace sr {
class LcdTouch {
    int fd=-1;
    input_absinfo ax{},ay{};
    int x=0,y=0;
    bool down=false,ignoreHeld=false;
    StripContact contact;
    std::array<double,6> calibration{0,-1,1,1,0,0}; // Landscape, rotate=90.
    std::chrono::steady_clock::time_point retry{};
    void disconnect() {
        if(fd>=0)close(fd);
        fd=-1;contact={};down=false;
        retry=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    }
    bool connect() {
        std::error_code error;
        for(const auto& entry:std::filesystem::directory_iterator("/dev/input",error)) {
            if(entry.path().filename().string().rfind("event",0)!=0)continue;
            int candidate=open(entry.path().c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);
            if(candidate<0)continue;
            char name[128]{};
            ioctl(candidate,EVIOCGNAME(sizeof(name)),name);
            if(!std::strstr(name,"ADS7846")||ioctl(candidate,EVIOCGABS(ABS_X),&ax)<0||
               ioctl(candidate,EVIOCGABS(ABS_Y),&ay)<0||ax.maximum<=ax.minimum||ay.maximum<=ay.minimum) {
                close(candidate);continue;
            }
            fd=candidate;x=ax.value;y=ay.value;
            unsigned long keys[(KEY_MAX+8*sizeof(unsigned long))/(8*sizeof(unsigned long))]{};
            if(ioctl(fd,EVIOCGKEY(sizeof(keys)),keys)<0) {disconnect();return false;}
            ignoreHeld=bool(keys[BTN_TOUCH/(8*sizeof(unsigned long))]&(1ul<<(BTN_TOUCH%(8*sizeof(unsigned long)))));
            std::cout<<"Touch strip: "<<entry.path()<<" / "<<name<<'\n'<<std::flush;
            return true;
        }
        retry=std::chrono::steady_clock::now()+std::chrono::seconds(1);return false;
    }
public:
    LcdTouch() {
        std::ifstream in("touch-calibration.conf");
        if(!in)return;
        std::string header;in>>header;
        if(header!="SR_TOUCH_V1")throw std::runtime_error("Invalid LCD touch calibration header");
        for(auto& value:calibration)if(!(in>>value)||!std::isfinite(value))
            throw std::runtime_error("Invalid LCD touch calibration coefficient");
    }
    ~LcdTouch() {if(fd>=0)close(fd);}
    LcdTouch(const LcdTouch&)=delete;
    LcdTouch& operator=(const LcdTouch&)=delete;
    std::vector<GestureSample> poll() {
        const bool connecting=fd<0;
        if(connecting&&(std::chrono::steady_clock::now()<retry||!connect()))return {{false,-1}};
        std::vector<GestureSample> samples;
        if(connecting)samples.push_back({true,-1});
        input_event event{};
        for(int count=0;count<512;++count) {
            const auto size=read(fd,&event,sizeof(event));
            if(size<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;
            if(size<0&&errno==EINTR)continue;
            if(size!=sizeof(event)) {disconnect();samples.push_back({false,-1});break;}
            if(event.type==EV_SYN&&event.code==SYN_DROPPED) {
                disconnect();samples.push_back({false,-1});break;
            }
            if(event.type==EV_KEY&&event.code==BTN_TOUCH)down=event.value!=0;
            if(event.type==EV_ABS&&event.code==ABS_X)x=event.value;
            if(event.type==EV_ABS&&event.code==ABS_Y)y=event.value;
            if(event.type!=EV_SYN||event.code!=SYN_REPORT)continue;
            if(ignoreHeld) {if(!down)ignoreHeld=false;continue;}
            const double nx=double(x-ax.minimum)/(ax.maximum-ax.minimum);
            const double ny=double(y-ay.minimum)/(ay.maximum-ay.minimum);
            const double px=(calibration[0]*nx+calibration[1]*ny+calibration[2])*479;
            const double py=(calibration[3]*nx+calibration[4]*ny+calibration[5])*319;
            samples.push_back(contact.update(std::clamp(px,0.0,479.0),std::clamp(py,0.0,319.0),down));
        }
        return samples;
    }
};
}
