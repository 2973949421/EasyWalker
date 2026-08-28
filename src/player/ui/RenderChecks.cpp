#include "NowPlayingPresenter.h"
#include <algorithm>

namespace adv_walkman { namespace player {
// Runs once before sound starts, using the production ImageBand, dispatcher,
// commit contract and the presenter's ONLY canvas. No SD writes or LCD frames.
const char* NowPlayingPresenter::bootRenderSelfCheck(){
    struct Size{int w,h,extent;};
    for(const auto size:{Size{120,144,188},Size{135,135,188},Size{135,133,188},Size{135,173,174}}){
        const int imageTop=size.extent==174?0:(size.extent-size.h)/2;
        FrameCommit frame;frame.begin(3,9,0,size.extent,false);
        for(int y=0;y<size.extent;y+=18){
            const int h=std::min(18,size.extent-y);ImageBand band;
            prepareRow(h,0x0861,1);
            uint32_t untouched=0;for(int j=h;j<18;++j)for(int x=0;x<135;++x)untouched=untouched*31+row_.readPixel(x,j);
            const auto result=dispatchContent(false,true,true,[&](){
                if(!band.begin(row_,y,h,imageTop,size.w,size.h))return RenderStep::Failed;
                return band.ready()?RenderStep::Submitted:RenderStep::Waiting;
            },[&](){return band.active();});
            if(result==RenderStep::Idle){prepareRow(3,0xFFFF,1);return "render_dispatch_fallthrough";}
            if(result==RenderStep::Failed)return "render_band_begin";
            uint8_t bytes[512];
            while(!band.ready()){
                const unsigned offset=band.offset(0),n=band.request();
                for(unsigned i=0;i<n;i+=2){const unsigned pixel=(offset+i)/2;
                    const uint16_t value=uint16_t(0x2000U|((pixel/size.w*37+pixel%size.w)&0x1FFF));
                    bytes[i]=value&255;bytes[i+1]=value>>8;}
                if(!band.append(bytes,n))return "render_band_append";
            }
            for(int j=0;j<h;++j)for(int x=0;x<135;++x){
                const int iy=y+j-imageTop,ix=x-(135-size.w)/2;
                const uint16_t expected=iy>=0&&iy<size.h&&ix>=0&&ix<size.w?
                    uint16_t(0x2000U|((iy*37+ix)&0x1FFF)):0x0861;
                if(row_.readPixel(x,j)!=expected)return "render_band_pixels";
            }
            uint32_t after=0;for(int j=h;j<18;++j)for(int x=0;x<135;++x)after=after*31+row_.readPixel(x,j);
            if(after!=untouched)return "render_band_outside_height";
            if(!frame.submit(3,9,y,h)||frame.submit(3,9,y,h))return "render_band_commit";
            band.cancel();
        }
        if(!frame.complete())return "render_frame_incomplete";
    }
    ImageBand bad;prepareRow(18,0,1);bad.begin(row_,0,18,0,135,135);
    uint8_t pair[2]={0,0};row_.setClipRect(0,0,135,3);
    if(bad.append(pair,2)||bad.ready())return "render_changed_canvas_accepted";
    bad.cancel();prepareRow(18,0,1);
    if(bad.append(pair,2))return "render_cancelled_append";
    // The historical bug changed the underlying canvas height, not only its
    // clip. Exercise that exact invalid surface then restore the sole buffer.
    row_.setBuffer(pixels_,135,3,16);
    const bool accepted=bad.begin(row_,0,3,0,135,135);bad.cancel();
    row_.setBuffer(pixels_,135,18,16);prepareRow(18,0,1);
    if(accepted)return "render_short_canvas_accepted";
    row_.setFont(&fonts::Font0);return nullptr;
}
} }
