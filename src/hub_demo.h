#pragma once
#include "gesture_sample.h"
#include "progsole_font.h"
#include "bitbuntu_font.h"
#include "tom_thumb_font.h"
#include "screens/reading_digits.h"
#include "mesh/gateway.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace sr {
// Climate uses assigned Mesh readings; other module actions remain simulated.
class HubDemo {
    std::vector<mesh::MenuEntry> menu=mesh::defaultMenu();
    struct Controls {int light=65,blinds=40,setpoint=220;bool lightOn=true,plug=false,thermoOn=true;};
    std::vector<Controls> settings=std::vector<Controls>(6);
    mesh::Reading climate;
    int type() const {return menu.empty()?-1:int(menu[selected].type);}
    bool climateFresh=false;
    std::string temperature(bool symbol=false) const {
        if(!climateFresh)return "--";
        char text[20];std::snprintf(text,sizeof(text),"%.1f%s",climate.temperature/100.0,symbol?"^":"C");return text;
    }
    std::string humidity() const {return climateFresh?std::to_string((climate.humidity+50)/100)+"%":"--";}
    inline static constexpr std::array<RGB,6> colors={RGB{137,202,221},RGB{160,206,151},RGB{239,201,139},RGB{188,170,223},RGB{169,191,229},RGB{224,166,145}};
    enum Page { Home, Menu, Module, Quick } page=Home;
    double quickOpening=0;
    bool brightnessAdjust=false;
    int panelBrightness=50,brightnessRequest=-1;
    double target=0,offset=0,opening=0,dimming=0,origin=0,last=0;
    int selected=0;
    bool dragging=false,swiped=false,held=false,quickAdjust=false,dismissed=false;
    int64_t since=0,previous=0,pulse=-1;
    template<size_t Count,size_t Height>
    static void fontLabel(Framebuffer& f,int y,std::string_view s,RGB color,
                          const uint8_t (&font)[Count][Height],int columns,int minY=0) {
        auto bounds=[&](unsigned char ch) {
            int first=columns,last=-1;
            if(ch>=32&&ch<32+Count)for(size_t row=0;row<Height;++row)for(int col=0;col<columns;++col)
                if(font[ch-32][row]&(1<<(columns-1-col))) {first=std::min(first,col);last=std::max(last,col);}
            // Keep an intentional word space; visible glyphs get exactly one blank column.
            return last<first?std::array<int,2>{0,1}:std::array<int,2>{first,last};
        };
        int width=0;
        for(unsigned char ch:s) {const auto span=bounds(ch);width+=span[1]-span[0]+2;}
        if(!s.empty())--width;
        int x=(64-width)/2;
        for(unsigned char ch:s) {
            const auto span=bounds(ch);
            if(ch>=32&&ch<32+Count)for(size_t row=0;row<Height;++row)for(int col=span[0];col<=span[1];++col)
                if(y+int(row)>=minY&&(font[ch-32][row]&(1<<(columns-1-col))))
                    f.pixel(x+col-span[0],y+int(row),color);
            x+=span[1]-span[0]+2;
        }
    }
    static void label(Framebuffer& f,int y,std::string_view s,RGB color,int minY=0) {
        fontLabel(f,y,s,color,progsoleFont,6,minY);
    }
    static void tinyLabel(Framebuffer& f,int y,std::string_view s,RGB color,int minY=0) {
        fontLabel(f,y,s,color,bitbuntuFont,7,minY);
    }
    static void microLabel(Framebuffer& f,int y,std::string_view s,RGB color) {
        fontLabel(f,y,s,color,tomThumbFont,3);
    }
    static void number(Framebuffer& f,int y,std::string_view s,RGB color=white) {
        const int left=(64-int(s.size())*8+1)/2;
        int advance=0;
        auto dot=[&](int x,int row) {f.pixel(left+advance+x,y+row,color);};
        for(char ch:s) {
            if((ch>='0'&&ch<='9')||ch=='^'||ch=='%') {
                const auto& rows=(ch=='^'||ch=='%')?screens::readingSymbols[ch=='%'?1:0]:screens::readingDigits[ch-'0'];
                for(int row=0;row<13;++row)for(int col=0;col<7;++col)
                    if(rows[row]&(1<<(6-col)))dot(col,row);
            } else if(ch>=32&&ch<=126) {
                for(int row=0;row<10;++row)for(int col=0;col<6;++col)
                    if(progsoleFont[ch-32][row]&(1<<(5-col)))dot(col+1,row+3);
            }
            advance+=8;
        }
    }
    int index() const {if(menu.empty())return 0;return std::clamp(int(std::round(target)),0,int(menu.size())-1);}
    bool adjustable() const {return type()==2||type()==4||type()==5;}
    void adjust(double position) {
        if(!controlMoving||controlModule!=selected) {
            controlValue=type()==2?settings[selected].light:(type()==4?settings[selected].blinds:settings[selected].setpoint);
            controlModule=selected;
        }
        // The LCD slider already maps its visible track to the full range.
        const double fraction=std::clamp(position,0.0,1.0);
        controlTarget=type()==5?100+200*fraction:100*fraction;
        controlMoving=true;
    }
    void toggle() {
        if(type()==2)settings[selected].lightOn=!settings[selected].lightOn;
        else if(type()==3)settings[selected].plug=!settings[selected].plug;
        else if(type()==4)settings[selected].blinds=settings[selected].blinds>0?0:100;
        else if(type()==5)settings[selected].thermoOn=!settings[selected].thermoOn;
    }
    std::string value() const {
        if(type()==0)return temperature()+" "+humidity();
        if(menu.empty())return "";
        if(type()==1)return "32%";
        if(type()==2)return settings[selected].lightOn?std::to_string(settings[selected].light)+"%":"OFF";
        if(type()==3)return settings[selected].plug?"ON":"OFF";
        if(type()==4)return std::to_string(settings[selected].blinds)+"%";
        return settings[selected].thermoOn?std::to_string(settings[selected].setpoint/10)+"."+std::to_string(settings[selected].setpoint%10)+"C":"OFF";
    }
    void click(int64_t now) {
        pulse=now;
        if(page==Home)toggle();
        else if(page==Menu&&!menu.empty()) {selected=index();target=offset=selected;page=Module;}
        else if(page==Module)toggle();
    }
public:
    int takeBrightnessRequest() {int value=brightnessRequest;brightnessRequest=-1;return value;}
    int displayBrightness(int saved) const {return brightnessAdjust?panelBrightness:saved;}
    void draw(Framebuffer& f,const GestureSample& input,int64_t now,const mesh::View& state={},int webPort=8080) {
        const double dt=previous?std::clamp(double(now-previous)/1000000,0.0,0.1):1.0/60;
        previous=now;
        if(menu!=state.menu) {
            menu=state.menu;settings.assign(menu.size(),Controls{});selected=0;target=offset=0;page=Home;
            quickAdjust=controlMoving=false;dragging=false;dimming=opening=0;
        }
        climate={};
        if(!menu.empty()&&type()==0)for(const auto& reading:state.readings)if(reading.address==menu[selected].address)climate=reading;
        climateFresh=node_is_fresh(climate.valid,climate.received,now);
        if(!brightnessAdjust)panelBrightness=state.brightness;
        if(input.healthy&&input.position>=0) {
            if(!dragging) {origin=last=input.position;since=now;swiped=held=dismissed=false;}
            if(!swiped&&!held&&now-since>=700000) {
                held=true;pulse=now;
                if(page==Home)quickAdjust=adjustable();
                else if(page==Quick)brightnessAdjust=true;
                else if(page==Module) {page=Menu;target=offset=selected;}
                else {selected=index();page=Home;}
                last=input.position;
            }
            if(brightnessAdjust)panelBrightness=int(std::round(100*std::clamp(input.position,0.0,1.0)));
            else if(quickAdjust)adjust(input.position);
            else if(!held) {
                if(!swiped&&std::abs(input.position-origin)>0.08) {swiped=true;last=origin;}
                if(swiped) {
                    const double delta=input.position-last;
                    if(page==Home) {if(!dismissed) {target=offset=selected;if(delta<0)page=Quick;else if(!menu.empty())page=Menu;dismissed=true;}}
                    else if(page==Quick) {if(!dismissed&&input.position-origin>.12) {page=Home;dismissed=true;}}
                    else if(page==Menu) {
                        if(!dismissed) {
                            target=std::clamp(target+delta*4,-0.5,double(menu.size()-1));
                            if(target<=-0.35) {selected=0;target=0;page=Home;dismissed=true;}
                        }
                    }
                    else if(adjustable())adjust(input.position);
                }
            }
            last=input.position;dragging=true;
        } else {
            if(dragging) {
                if(brightnessAdjust&&input.healthy)brightnessRequest=panelBrightness;
                if(input.healthy&&!swiped&&!held&&now-since<=180000)click(now);
                if(page==Menu) {target=std::round(target);selected=index();}
            }
            dragging=false;quickAdjust=false;brightnessAdjust=false;
            if(!input.healthy)pulse=-1;
        }
        if(!input.healthy||controlModule!=selected)controlMoving=false;
        if(controlMoving) {
            controlValue+=(controlTarget-controlValue)*(1-std::exp(-dt/0.16));
            if(std::abs(controlTarget-controlValue)<0.05) {controlValue=controlTarget;controlMoving=false;}
            int& setting=type()==2?settings[selected].light:(type()==4?settings[selected].blinds:settings[selected].setpoint);
            setting=int(std::round(controlValue));
        }
        offset+=(target-offset)*(1-std::exp(-dt/0.09));
        opening+=((page==Menu||page==Module?1.0:0.0)-opening)*(1-std::exp(-dt/0.12));
        quickOpening+=((page==Quick?1.0:0.0)-quickOpening)*(1-std::exp(-dt/0.12));
        dimming+=((quickAdjust?1.0:0.0)-dimming)*(1-std::exp(-dt/0.22));
        if(!quickAdjust&&dimming<0.001)dimming=0;
        if(page!=Home)dimming=0;
        f.clear();
        const auto wall=std::time(nullptr);std::tm local{};localtime_r(&wall,&local);
        char clock[6];std::snprintf(clock,sizeof(clock),"%02d:%02d",local.tm_hour,local.tm_min);
        const RGB accent=menu.empty()?colors[0]:colors[menu[page==Menu?index():selected].type];
        if(page!=Module) {
            const int clockY=int(std::round(12*(1-opening)));
            if(opening<0.5)number(f,clockY,clock,white);
            else microLabel(f,clockY,clock,white);
            if(opening<0.99) {
                const int shift=int(opening*35);
                microLabel(f,40+shift,(menu.empty()?"NO MODULES":menu[selected].name),brightness(accent,int(255*(1-opening))));
                microLabel(f,51+shift,value(),brightness(white,int(230*(1-opening))));
            }
            if(opening>0.01) {
                const int first=int(std::floor(offset))-1;
                for(int row=first;row<=first+3;++row) {
                    if(row<0||row>=int(menu.size()))continue;
                    const double distance=row-offset,focus=std::max(0.0,1-std::abs(distance));
                    const int n=row;
                    const int y=int(std::round(30+distance*17+(1-opening)*55));
                    const RGB color=brightness(focus>0.5?colors[menu[n].type]:white,int((48+207*focus)*opening));
                    const int menuTop=clockY+(opening<0.5?13:6)+2;
                    tinyLabel(f,y,menu[n].name,color,menuTop);
                }
                f.rect(62,31,2,9,brightness(accent,int(200*opening)));
            }
        } else {
            tinyLabel(f,2,(menu.empty()?"NO MODULES":menu[selected].name),accent);
            if(type()==0) {
                number(f,19,temperature(true),accent);
                number(f,42,humidity(),white);
            } else if((type()==2&&!settings[selected].lightOn)||type()==3||(type()==5&&!settings[selected].thermoOn))label(f,24,value(),white);
            else number(f,22,type()==5?std::to_string(settings[selected].setpoint/10)+"."+std::to_string(settings[selected].setpoint%10)+"^":value());
            if(adjustable()) {
                const int amount=type()==2?(settings[selected].lightOn?settings[selected].light:0):(type()==4?settings[selected].blinds:(settings[selected].thermoOn?(settings[selected].setpoint-100)/2:0));
                const int fill=30*amount/100;
                f.rect(60,19,2,30,brightness(accent,40));
                f.rect(60,49-fill,2,fill,accent);
                tinyLabel(f,49,type()==2?(settings[selected].lightOn?"ON":"OFF"):(type()==4?"POSITION":(settings[selected].thermoOn?"SET TEMP":"OFF")),accent);
            }
            else if(type()==1)tinyLabel(f,45,"MOISTURE",accent);
            else if(type()==3)tinyLabel(f,45,"TAP ON/OFF",accent);
        }
        if(page==Home&&dimming>0) {
            // Fade the home layout in both directions; preserve the value's contrast.
            const int level=int(std::round(255-235*dimming));
            for(auto& pixel:f.pixels)pixel=brightness(pixel,level);
            f.rect(0,51,64,13,black);
            const std::string setting=type()==2?std::to_string(settings[selected].light)+"%":
                (type()==4?std::to_string(settings[selected].blinds)+"%":std::to_string(settings[selected].setpoint/10)+"."+std::to_string(settings[selected].setpoint%10)+"C");
            microLabel(f,51,quickAdjust?setting:value(),brightness(white,int(std::round(230+25*dimming))));
            const int amount=type()==2?settings[selected].light:(type()==4?settings[selected].blinds:(settings[selected].setpoint-100)/2);
            const int fill=30*amount/100;
            f.rect(60,19,2,30,brightness(accent,int(40*dimming)));
            f.rect(60,49-fill,2,fill,brightness(accent,int(255*dimming)));
        }
        if(pulse>=0&&now-pulse<180000) {
            const int level=int(160*std::sin(3.141592653589793*double(now-pulse)/180000));
            f.rect(62,28,2,16,brightness(accent,level));
        }
        if(quickOpening>.001) {
            Framebuffer panel;
            const RGB accent{137,202,221};
            microLabel(panel,1,"QUICK SETTINGS",accent);
            microLabel(panel,11,"BRIGHTNESS",white);
            tinyLabel(panel,18,std::to_string(panelBrightness)+"%",accent);
            panel.rect(60,11,2,15,brightness(accent,45));
            const int fill=15*panelBrightness/100;panel.rect(60,26-fill,2,fill,accent);
            microLabel(panel,30,"NODES "+std::to_string(state.nodeCount),white);
            if(state.ip.empty())microLabel(panel,43,"WI-FI OFF",dim);
            else {
                std::string name;
                for(unsigned char ch:state.wifiName) {
                    if(ch>=32&&ch<127)name+=char(ch);
                    else if((ch&0xc0)!=0x80)name+='?';
                }
                if(name.empty())name="WI-FI CONNECTED";
                if(name.size()>15) {name+="   ";const size_t start=size_t(now/500000)%name.size();name=(name+name).substr(start,15);}
                microLabel(panel,40,name,accent);
                microLabel(panel,50,state.ip,white);
                microLabel(panel,58,":"+std::to_string(webPort),accent);
            }
            const int top=-int(std::round(64*(1-quickOpening)));
            for(int y=0;y<64;++y)for(int x=0;x<64;++x)f.pixel(x,y+top,panel.at(x,y));
        }
        if(!input.healthy)f.pixel(63,0,{255,40,30});
    }
private:
    double controlValue=0,controlTarget=0;
    int controlModule=-1;
    bool controlMoving=false;
};
}
