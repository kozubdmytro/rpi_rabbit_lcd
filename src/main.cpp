#include "screens/readings.h"
#include "mesh/bluez.h"
#include "web.h"
#include "renderer.h"
#include "hub_demo.h"
#include "lcd_touch.h"
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include <stdexcept>

namespace { volatile std::sig_atomic_t running=1; void stop(int) {running=0;} }
int main(int argc,char** argv) {
    try {
        bool headless=false,create=false;
        int width=480,height=320,frames=0,fps=60;
        std::string dump,framebufferDevice="/dev/fb1";
        std::string stateDirectory="mesh-state",phoneAdapter="hci0";
        std::string webDirectory="web";
        int webPort=8080;
        auto value=[&](int& i){if(i+1>=argc)throw std::runtime_error(std::string("Missing value for ")+argv[i]);return std::string(argv[++i]);};
        for(int i=1;i<argc;++i) {
            std::string a=argv[i];
            if(a=="--headless")headless=true;
            else if(a=="--fb")framebufferDevice=value(i);
            else if(a=="--create-network")create=true;
            else if(a=="--state-dir")stateDirectory=value(i);
            else if(a=="--phone-adapter")phoneAdapter=value(i);
            else if(a=="--web-port")webPort=std::stoi(value(i));
            else if(a=="--web-dir")webDirectory=value(i);
            else if(a=="--width")width=std::stoi(value(i));
            else if(a=="--height")height=std::stoi(value(i));
            else if(a=="--frames")frames=std::stoi(value(i));
            else if(a=="--fps")fps=std::stoi(value(i));
            else if(a=="--dump")dump=value(i);
            else if(a=="--help") {
                std::cout<<"Smart Rabbit Mesh gateway (C++17)\n"
                    "  --create-network       First initialization only; refuses existing state\n"
                    "  --state-dir PATH       Durable gateway state (default mesh-state)\n"
                    "  --phone-adapter hci0   bluetoothd GATT/advertising adapter\n"
                    "  --web-port 8080        Local web interface port\n"
                    "  --web-dir web          Local web assets directory\n"
                    "  --fb /dev/fb1          LCD framebuffer (must be 480x320)\n"
                    "  --headless             Clock/menu preview, no Bluetooth/state I/O\n"
                    "  --frames N --fps 60 --dump PATH --width 480 --height 320\n"
                    "Clock and module interface; Ctrl+C exits.\n";
                return 0;
            } else throw std::runtime_error("Unknown option: "+a);
        }
        if(frames<0||fps<1||fps>240)throw std::runtime_error("Invalid frame count/FPS");
        if(webPort<1||webPort>65535)throw std::runtime_error("Invalid web port");
        if(create&&headless)throw std::runtime_error("Network creation cannot be combined with preview");
        std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
        auto display=headless?sr::createHeadless(width,height):sr::createLcdDisplay(framebufferDevice);
        const auto& viewport=display->viewport();
        std::cout<<"Display: "<<display->name()<<"; initial brightness 50%\n"<<std::flush;
        display->setBrightness(0);
        sr::Framebuffer blank;display->present(blank);
        std::unique_ptr<sr::mesh::BluezGateway> gateway;
        if(!headless)gateway=std::make_unique<sr::mesh::BluezGateway>(stateDirectory,phoneAdapter,create);
        std::unique_ptr<sr::WebServer> web;
        if(gateway) {
            web=std::make_unique<sr::WebServer>(unsigned(webPort),webDirectory,[&](const std::string& path,const std::string& body){return gateway->webRequest(path,body);});
            std::cout<<"Local web interface: http://<Wi-Fi IP>:"<<webPort<<"\n"<<std::flush;
        }
        sr::Framebuffer frame;int rendered=0;bool offPresented=false;
        sr::HubDemo demo;sr::GestureSample touchSample{};
        std::unique_ptr<sr::LcdTouch> touch;
        if(!headless)touch=std::make_unique<sr::LcdTouch>();
        int64_t fadeStart=-1;
        using Clock=std::chrono::steady_clock;auto next=Clock::now();
        while(running&&(frames==0||rendered<frames)) {
            auto state=gateway?gateway->view():sr::mesh::View{};
            if(!state.error.empty())throw std::runtime_error("Gateway unavailable: "+state.error);
            const int64_t tick=sr::mesh::monotonicUs();
            if(touch)for(const auto& sample:touch->poll()) {
                touchSample=sample;demo.draw(frame,touchSample,tick,state,webPort);
            }
            demo.draw(frame,touchSample,tick,state,webPort);
            const int brightnessRequest=demo.takeBrightnessRequest();
            if(gateway&&brightnessRequest>=0)gateway->requestBrightness(brightnessRequest);
            const int level=demo.displayBrightness(state.brightness);
            display->setBrightness(level);
            if(level) {
                if(fadeStart<0)fadeStart=tick;
                const double progress=std::clamp(double(tick-fadeStart)/1000000,0.0,1.0);
                const double fade=progress*progress*(3-2*progress);
                for(auto& pixel:frame.pixels)pixel={uint8_t(pixel.r*fade),uint8_t(pixel.g*fade),uint8_t(pixel.b*fade)};
                sr::screens::rotate(frame,state.rotation);
                display->present(frame);offPresented=false;
            } else if(!offPresented) {frame.clear();display->present(frame);offPresented=true;}
            ++rendered;next+=std::chrono::microseconds(1000000/fps);if(next<Clock::now())next=Clock::now();std::this_thread::sleep_until(next);
        }
        if(!dump.empty()) {
            sr::writePPM(dump+"-logical.ppm",64,64,frame.pixels.data());auto preview=frame;
            unsigned percent=gateway?gateway->view().brightness:50;
            for(auto& pixel:preview.pixels)pixel={uint8_t(pixel.r*percent/100),uint8_t(pixel.g*percent/100),uint8_t(pixel.b*percent/100)};
            auto scaled=sr::scaleFrame(preview,viewport);
            sr::drawTouchStrip(scaled,viewport.width,viewport.height,int(percent));
            sr::writePPM(dump+"-preview.ppm",viewport.width,viewport.height,scaled.data());
        }
        return 0;
    } catch(const std::exception& e) {std::cerr<<"Smart Rabbit error: "<<e.what()<<'\n';return 1;}
}
