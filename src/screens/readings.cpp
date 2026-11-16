#include "readings.h"
namespace sr::screens {
void rotate(Framebuffer& frame,uint8_t rotation) {
    if(rotation==1)return;auto original=frame;
    for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        int nx=x,ny=y;
        if(rotation==2){nx=63-y;ny=x;}else if(rotation==3){nx=63-x;ny=63-y;}else if(rotation==4){nx=y;ny=63-x;}
        frame.pixel(nx,ny,original.at(x,y));
    }
}
}
