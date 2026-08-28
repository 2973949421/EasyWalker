#include "LibraryVisual.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include "NowPlayingModel.h"
namespace adv_walkman { namespace player {
namespace {
constexpr uint16_t bg=0x0861,accent=0xFBE0,muted=0x8410;
void disc(lgfx::LGFXBase& row,int x,int y,int radius,bool selected){
    row.fillCircle(x,y,radius,0x0000);
    for(int r=radius;r>10;r-=4)row.drawCircle(x,y,r,selected?0x3186:0x18C3);
    row.fillCircle(x,y,9,selected?accent:0x4208);row.fillCircle(x,y,2,bg);
}
// Render cached coverage directly into this stripe; no bitmap/Sprite allocation.
void arc(lgfx::LGFXBase& row,FontCache& cache,const char* text,int cx,int cy,int stripeY,float rotation,int top,uint16_t color){
    uint32_t cps[10]{};unsigned count=0;bool bad=false;float advances[10]{},total=0;
    while(*text&&count<10){const auto cp=mediaCodepoint(text,bad);const auto* g=cache.find(cp,FontCache::libraryFace(cp,14));
        const float advance=g?std::max<int>(g->advance,4):12;
        if(total+advance>46)break;
        cps[count]=cp;advances[count++]=advance;total+=advance;}
    float cursor=0;
    for(unsigned k=0;k<count;++k){
        const float angle=(cursor+advances[k]*.5f-total*.5f)/19.0f+rotation;cursor+=advances[k];
        const float co=std::cos(angle),si=std::sin(angle);
        const auto* g=cache.find(cps[k],FontCache::libraryFace(cps[k],14));const auto* bits=g?cache.bitmap(*g):nullptr;if(!bits)continue;
        const int px=cx+std::lround(19*si),py=cy-std::lround(19*co);
        const int halfW=int(std::ceil((g->width*std::fabs(co)+g->height*std::fabs(si))*.5f));
        const int halfH=int(std::ceil((g->height*std::fabs(co)+g->width*std::fabs(si))*.5f));
        if(px-halfW<0||px+halfW>=135||py-halfH<top||py+halfH>=240)continue; // whole glyph or none
        for(int j=0;j<g->height;++j)for(int i=0;i<g->width;++i){
            const int dx=i-int(g->width)/2,dy=j-int(g->height)/2;
            const int x=px+std::lround(dx*co-dy*si),y=py+std::lround(dx*si+dy*co)-stripeY;
            if(y<0||y>=row.height()||x<0||x>=135)continue;
            const unsigned p=j*g->width+i,a=(bits[p/2]>>(p%2?0:4))&15;
            if(a){const uint16_t base=row.readPixel(x,y);const unsigned r=(((base>>11)&31)*(15-a)+((color>>11)&31)*a)/15;
                const unsigned gr=(((base>>5)&63)*(15-a)+((color>>5)&63)*a)/15,b=((base&31)*(15-a)+(color&31)*a)/15;
                row.drawPixel(x,y,(r<<11)|(gr<<5)|b);}
        }
    }
}
}
void LibraryVisual::select(const char* path,int direction){
    // Retarget the visible phase, rather than enqueueing obsolete motions.
    animationFrom_=std::max(-2.f,std::min(2.f,animationFrom_*(3-int(animationStep_))/3.f+direction));
    visible_=true;cover.select(path);invalidate();previousState_=MediaState::Loading;
    namesReady_=0;animationAt_=0;animationStep_=direction?0:3;
}
void LibraryVisual::setShortName(unsigned slot,const char* text){
    if(slot>2)return;
    const char* p=text;bool invalid=false;unsigned count=0;
    while(*p&&count<10){mediaCodepoint(p,invalid);++count;}
    const size_t n=std::min<size_t>(p-text,sizeof(shortNames_[slot])-1);
    std::memcpy(shortNames_[slot],text,n);shortNames_[slot][n]=0;namesReady_|=1U<<slot;
}
bool LibraryVisual::prepare(FontCache& fonts,const UiRenderContext& c){
    if(!fonts.requestUiMetrics(c.catalogCount?c.libraryName:"暂无曲库",22,true))return false;
    if(c.catalogCount){if(namesReady_!=7)return false;
        for(const auto& text:shortNames_)if(!fonts.requestUiWindow(text,14,0,70,1,true))return false;}
    return true;
}
void LibraryVisual::drawDiscs(M5Canvas& row,FontCache& fonts,const UiRenderContext& c,int y,uint32_t){
    if(!c.catalogCount)return;
    struct Pose{int index,x,y;float angle;};Pose poses[3];unsigned count=0;
    const float offset=animationFrom_*(3-int(animationStep_))/3.f;
    for(int k=-1;k<=1;++k){
        const float angle=(k+offset)*.6981317f;
        poses[count++]={k,67+int(std::lround(60*std::sin(angle))),kLibraryWheelTop+88-int(std::lround(60*std::cos(angle))),angle};
    }
    // Furthest/backmost first. The current slot is at the front at rest.
    std::sort(poses,poses+count,[](const Pose& a,const Pose& b){return std::fabs(a.angle)>std::fabs(b.angle);});
    for(unsigned i=0;i<count;++i){const auto& p=poses[i];disc(row,p.x,p.y-y,26,p.index==0);
        arc(row,fonts,shortNames_[p.index+1],p.x,p.y,y,p.angle,kLibraryWheelTop,p.index==0?0x65FF:0x43D7);}
}
bool LibraryVisual::drawName(M5GFX& display,FontCache& fonts,const UiRenderContext& c,uint32_t now){
    const char* text=c.catalogCount?c.libraryName:"暂无曲库";
    if(!nameMeasured_){
        if(!fonts.requestUiMetrics(text,22,true))return false;
        bool invalid=false;int advance=0,left=0,right=0;const char* p=text;
        while(*p){const auto cp=mediaCodepoint(p,invalid);const auto* g=fonts.find(cp,FontCache::libraryFace(cp,22));
            if(!g)return false;left=std::min(left,advance+g->dx);right=std::max(right,advance+g->dx+g->width);advance+=g->advance;}
        nameLeft_=left;nameWidth_=std::max(right,advance)-left;nameMeasured_=true;nameEpoch_=now;
        nameLayout.lineCount=1;nameLayout.availableWidthPx=123;nameLayout.maxLineWidthPx=std::min<int>(123,nameWidth_);
        nameLayout.invalidUtf8=invalid;
    }
    const uint32_t cycle=2*NowPlayingModel::kHoldMs+NowPlayingModel::scrollMs(nameWidth_);
    const int offset=NowPlayingModel::offsetAt(nameWidth_,(now-nameEpoch_)%cycle);
    if(nameDone_&&(now-nameTick_<NowPlayingModel::kAnimationIntervalMs||offset==nameOffset_))return false;
    // UI pins last until this redraw is complete, not for an unbounded marquee.
    // Arc glyphs are repinned too before a later wheel redraw.
    if(!fonts.requestUiWindow(text,22,std::max(0,offset+nameLeft_-22),167,1,true))return false;
    int x=nameWidth_<=123?(135-nameWidth_)/2-nameLeft_:6-nameLeft_-offset;
    display.setClipRect(6,kLibraryNameTop,123,kLibraryNameHeight);display.fillRect(0,kLibraryNameTop,135,kLibraryNameHeight,bg);
    const char* p=text;bool invalid=false;
    while(*p){const auto cp=mediaCodepoint(p,invalid);const auto face=FontCache::libraryFace(cp,22);const auto* g=fonts.find(cp,face);
        if(!g)break;
        if(x+g->dx+g->width>6&&x+g->dx<129)fonts.draw(display,cp,face,x,kLibraryNameTop,TFT_WHITE,bg);
        x+=g->advance;}
    display.clearClipRect();nameDone_=true;nameOffset_=offset;nameTick_=now;
    fonts.clearPins(FontCache::Ui);return true;
}
bool LibraryVisual::render(M5GFX& display,M5Canvas& row,FontCache& fonts,const UiRenderContext& c,uint32_t now){
    if(!visible_)return false;
    if(clear_){display.fillRect(0,0,135,240,bg);clear_=false;return true;}
    if(!cover.bandActive()&&drawName(display,fonts,c,now))return true;
    if(!nameDone_)return false;
    if(cover.state()!=previousState_){previousState_=cover.state();imageY_=0;}
    if(!animationAt_)animationAt_=now;
    const uint8_t step=wheelAnimationStep(animationStep_,now-animationAt_);
    if(step!=animationStep_ && bottomY_>=240){animationStep_=step;bottomY_=kLibraryWheelTop;}
    if(bottomY_<240&&!cover.bandActive()){
        if(c.catalogCount)for(const auto& text:shortNames_)if(!fonts.requestUiWindow(text,14,0,70,1,true))return false;
        const int height=std::min(18,240-bottomY_);row.clearClipRect();row.setClipRect(0,0,135,height);row.fillScreen(bg);
        drawDiscs(row,fonts,c,bottomY_,now);
        display.setClipRect(0,bottomY_,135,height);row.pushSprite(&display,0,bottomY_);display.clearClipRect();row.setFont(&fonts::Font0);row.clearClipRect();
        bottomY_+=height;if(bottomY_>=240){++frames;fonts.clearPins(FontCache::Ui);}return true;
    }
    if(imageY_<174){
        const bool ready=cover.state()==MediaState::Ready;
        const int height=std::min(18,174-imageY_);
        if(!cover.bandActive()){row.clearClipRect();row.setClipRect(0,0,135,height);row.fillScreen(bg);}
        if(ready&&!cover.prepareBand(row,imageY_,height))return false;
        if(!ready){disc(row,67,85-imageY_,32,false);row.drawRect(0,-imageY_,135,174,muted);}
        display.setClipRect(0,imageY_,135,height);row.pushSprite(&display,0,imageY_);display.clearClipRect();row.clearClipRect();cover.finishBand();imageY_+=height;
        if(imageY_>=174&&ready)++coverFrames;return true;
    }
    return false;
}
} }
