#pragma once
#include <M5GFX.h>
#include <algorithm>
#include "MediaTypes.h"
namespace adv_walkman { namespace player {
// One owner for the existing 135x18 canvas. No pixel allocation here.
class ImageBand {
 public:
    bool active()const{return canvas_!=nullptr;}
    bool ready()const{return active()&&validCanvas()&&done_==length_;}
    void cancel(){canvas_=nullptr;done_=length_=0;}
    bool matches(const lgfx::LGFXBase& canvas,int y,int height)const{
        return canvas_==&canvas&&y_==y&&height_==height&&validCanvas();
    }
    bool begin(lgfx::LGFXBase& canvas,int y,int height,int imageTop,int width,int imageHeight){
        if(active())return matches(canvas,y,height);
        canvas_=&canvas;width_=width;y_=y;top_=imageTop;height_=height;
        if(width<=0||width>135||imageHeight<=0||!validCanvas()){cancel();return false;}
        const int first=std::max(y,imageTop),end=std::min(y+height,imageTop+imageHeight);
        start_=std::max(0,first-imageTop)*width*2;
        length_=std::max(0,end-first)*width*2;done_=0;return true;
    }
    uint32_t offset(uint32_t header)const{return header+start_+done_;}
    unsigned request()const{return std::min<uint32_t>(512,length_-done_);}
    bool append(const uint8_t* bytes,unsigned count){
        if(!active()||!validCanvas()||!count||count>512||(count&1)||count>length_-done_)return false;
        // Bulk scanline writes into the existing canvas instead of hundreds
        // of per-pixel virtual calls. Source is little-endian RGB565.
        lgfx::rgb565_t pixels[256];
        for(unsigned i=0;i<count/2;++i)pixels[i].raw=mediaU16(bytes+i*2);
        for(unsigned i=0;i<count/2;){
            const unsigned pixel=(start_+done_)/2+i;
            const unsigned n=std::min<unsigned>(count/2-i,width_-pixel%width_);
            canvas_->pushImage((135-width_)/2+pixel%width_,top_+pixel/width_-y_,n,1,pixels+i);i+=n;
        }
        done_+=count;return true;
    }
 private:
    lgfx::LGFXBase* canvas_=nullptr;
    uint32_t start_=0,length_=0,done_=0;
    int16_t width_=0,y_=0,top_=0,height_=0;
    bool validCanvas()const{
        if(!canvas_||canvas_->width()!=135||canvas_->height()!=18||height_<=0||height_>18)return false;
        int x,y,w,h;canvas_->getClipRect(&x,&y,&w,&h);
        return x==0&&y==0&&w==135&&h==height_;
    }
};
} }
