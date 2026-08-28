#pragma once
#include <M5GFX.h>
#include <algorithm>
#include "MediaTypes.h"
namespace adv_walkman { namespace player {
// One owner for the existing 135x18 canvas. No pixel allocation here.
class ImageBand {
 public:
    bool active()const{return canvas_!=nullptr;}
    bool ready()const{return active()&&done_==length_;}
    void cancel(){canvas_=nullptr;done_=length_=0;}
    void begin(lgfx::LGFXBase& canvas,int y,int height,int imageTop,int width,int imageHeight){
        if(active())return;
        canvas_=&canvas;width_=width;y_=y;top_=imageTop;
        const int first=std::max(y,imageTop),end=std::min(y+height,imageTop+imageHeight);
        start_=std::max(0,first-imageTop)*width*2;
        length_=std::max(0,end-first)*width*2;done_=0;
    }
    uint32_t offset(uint32_t header)const{return header+start_+done_;}
    unsigned request()const{return std::min<uint32_t>(512,length_-done_);}
    void append(const uint8_t* bytes,unsigned count){
        // Bulk scanline writes into the existing canvas instead of hundreds
        // of per-pixel virtual calls. Source is little-endian RGB565.
        lgfx::rgb565_t pixels[256];
        for(unsigned i=0;i<count/2;++i)pixels[i].raw=mediaU16(bytes+i*2);
        for(unsigned i=0;i<count/2;){
            const unsigned pixel=(start_+done_)/2+i;
            const unsigned n=std::min<unsigned>(count/2-i,width_-pixel%width_);
            canvas_->pushImage((135-width_)/2+pixel%width_,top_+pixel/width_-y_,n,1,pixels+i);i+=n;
        }
        done_+=count;
    }
 private:
    lgfx::LGFXBase* canvas_=nullptr;
    uint32_t start_=0,length_=0,done_=0;
    int16_t width_=0,y_=0,top_=0;
};
} }
