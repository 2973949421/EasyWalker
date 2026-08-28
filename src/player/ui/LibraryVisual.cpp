#include "LibraryVisual.h"
#include <cmath>
#include <cstring>
#include <algorithm>
namespace adv_walkman { namespace player {
namespace {
constexpr uint16_t bg=0x0861,accent=0xFBE0,muted=0x8410;
void disc(lgfx::LGFXBase& row,int x,int y,int radius,bool selected){
    row.fillCircle(x,y,radius,0x0000);
    for(int r=radius;r>10;r-=4)row.drawCircle(x,y,r,selected?0x3186:0x18C3);
    row.fillCircle(x,y,9,selected?accent:0x4208);row.fillCircle(x,y,2,bg);
}
// Render cached coverage directly into this stripe; no bitmap/Sprite allocation.
void arc(lgfx::LGFXBase& row,FontCache& cache,const char* text,int cx,int cy,int stripeY,float rotation,int top){
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
            if(a){const uint16_t base=row.readPixel(x,y);const unsigned r=(((base>>11)&31)*(15-a)+31*a)/15;
                const unsigned gr=(((base>>5)&63)*(15-a)+63*a)/15,b=((base&31)*(15-a)+31*a)/15;
                row.drawPixel(x,y,(r<<11)|(gr<<5)|b);}
        }
    }
}
}
void LibraryVisual::select(const char* path,int direction){
    // Retarget the visible phase, rather than enqueueing obsolete motions.
    animationFrom_=std::max(-2.f,std::min(2.f,animationFrom_*(3-int(animationStep_))/3.f+direction));
    visible_=true;cover.select(path);invalidate();previousState_=MediaState::Loading;
    direction_=direction;animationAt_=millis();animationStep_=direction?0:3;
}
bool LibraryVisual::prepare(FontCache& fonts,const UiRenderContext& c){
    if(!c.catalogCount&&!fonts.requestUiWindow("暂无曲库",22,0,246,1,true))return false;
    if(c.error&&c.error[0]&&!fonts.requestUiWindow(c.error,12,0,123,1))return false;
    if(!fonts.requestUiWindow(c.libraryName,22,0,270,1,true)||!fonts.requestUiWindow("...",22,0,123,1,true))return false;
    // Small arc has at most ten complete codepoints. Full title is independent.
    const char* p=c.libraryName;const char* start=p;bool invalid=false;unsigned count=0;
    while(*p&&count<10){mediaCodepoint(p,invalid);++count;}
    const size_t n=std::min<size_t>(p-start,sizeof(shortName_)-1);std::memcpy(shortName_,start,n);shortName_[n]=0;
    return fonts.requestUiWindow(shortName_,14,0,70,1,true);
}
void LibraryVisual::drawDiscs(M5Canvas& row,FontCache& fonts,const UiRenderContext& c,int y,uint32_t){
    if(!c.catalogCount)return;
    struct Pose{int index,x,y;float angle;};Pose poses[3];unsigned count=0;
    const float offset=animationFrom_*(3-int(animationStep_))/3.f;
    for(int k=-1;k<=1;++k){
        if(int(c.catalogIndex)+k<0||int(c.catalogIndex)+k>=int(c.catalogCount))continue;
        const float angle=(k+offset)*.6981317f;
        poses[count++]={k,67+int(std::lround(60*std::sin(angle))),wheelTop_+88-int(std::lround(60*std::cos(angle))),angle};
    }
    // Furthest/backmost first. The current slot is at the front at rest.
    std::sort(poses,poses+count,[](const Pose& a,const Pose& b){return std::fabs(a.angle)>std::fabs(b.angle);});
    for(unsigned i=0;i<count;++i){const auto& p=poses[i];disc(row,p.x,p.y-y,26,p.index==0);
        if(p.index==0)arc(row,fonts,shortName_,p.x,p.y,y,p.angle,wheelTop_);}
}
bool LibraryVisual::render(M5GFX& display,M5Canvas& row,FontCache& fonts,const UiRenderContext& c,uint32_t now){
    if(!visible_)return false;
    CachedUiFont face(&fonts,22,true);
    if(clear_){display.fillRect(0,0,135,240,bg);clear_=false;return true;}
    if(!nameDone_){display.setFont(&face);display.setTextSize(1);display.setTextColor(TFT_WHITE,bg);
        struct Centered {M5GFX* display;int line;} centered{&display,0};
        nameLayout=UiTextLayout::visitLines(display,c.catalogCount?c.libraryName:"暂无曲库",{6,174,123,44,2,0,true},
            [](const char* text,void* data){auto& c=*static_cast<Centered*>(data);
                c.display->drawString(text,(135-c.display->textWidth(text))/2,174+c.line++*22);},&centered);
        wheelTop_=174+std::max<int>(1,nameLayout.lineCount)*22;bottomY_=wheelTop_;
        display.setFont(&fonts::Font0);nameDone_=true;return true;}
    if(cover.state()!=previousState_){previousState_=cover.state();imageY_=0;}
    const uint8_t step=std::min<uint32_t>(3,(now-animationAt_)*3/160);
    if(step!=animationStep_ && bottomY_>=240){animationStep_=step;bottomY_=wheelTop_;}
    if(bottomY_<240&&!cover.bandActive()){
        const int height=std::min(18,240-bottomY_);row.clearClipRect();row.setClipRect(0,0,135,height);row.fillScreen(bg);
        drawDiscs(row,fonts,c,bottomY_,now);row.setFont(&face);row.setTextSize(1);row.setTextColor(accent,bg);
        display.setClipRect(0,bottomY_,135,height);row.pushSprite(&display,0,bottomY_);display.clearClipRect();row.setFont(&fonts::Font0);row.clearClipRect();
        bottomY_+=height;if(bottomY_>=240)++frames;return true;
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
