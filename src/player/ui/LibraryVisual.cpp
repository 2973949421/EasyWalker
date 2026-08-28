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
void arc(lgfx::LGFXBase& row,FontCache& cache,const char* text,int cx,int cy,int stripeY){
    uint32_t cps[10]{};unsigned count=0;bool bad=false;float advances[10]{},total=0;
    while(*text&&count<10){const auto cp=mediaCodepoint(text,bad);const auto* g=cache.find(cp,FontCache::faceFor(cp,12));
        const float advance=g?std::max<int>(g->advance,4):12;
        if(total+advance>64)break;
        cps[count]=cp;advances[count++]=advance;total+=advance;}
    float cursor=0;
    for(unsigned k=0;k<count;++k){
        const float angle=(cursor+advances[k]*.5f-total*.5f)/24.0f;cursor+=advances[k];
        const float co=std::cos(angle),si=std::sin(angle);
        const auto* g=cache.find(cps[k],FontCache::faceFor(cps[k],12));const auto* bits=g?cache.bitmap(*g):nullptr;if(!bits)continue;
        const int px=cx+std::lround(24*si),py=cy-std::lround(24*co);
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
    visible_=true;cover.select(path);invalidate();previousState_=MediaState::Loading;
    direction_=direction;animationAt_=millis();animationStep_=direction?0:3;
}
bool LibraryVisual::prepare(FontCache& fonts,const UiRenderContext& c){
    const char* fixed="左右选择 Enter进入 S设置 暂无曲库 封面不可用 加载失败 Enter重试 0123456789/";
    if(!fonts.requestUiWindow(fixed,12,0,2000,1))return false;
    if(c.error&&c.error[0]&&!fonts.requestUiWindow(c.error,12,0,123,1))return false;
    if(!fonts.requestUiWindow(c.libraryName,12,0,246,1))return false;
    // Small arc has at most ten complete codepoints. Full title is independent.
    const char* p=c.libraryName;const char* start=p;bool invalid=false;unsigned count=0;
    while(*p&&count<10){mediaCodepoint(p,invalid);++count;}
    const size_t n=std::min<size_t>(p-start,sizeof(shortName_)-1);std::memcpy(shortName_,start,n);shortName_[n]=0;
    return fonts.requestUiWindow(shortName_,12,0,240,1);
}
void LibraryVisual::drawDiscs(M5Canvas& row,FontCache& fonts,const UiRenderContext& c,int y,uint32_t){
    if(!c.catalogCount)return;
    const int shift=direction_*(3-int(animationStep_))*5;
    // Boundary neighbours, not wrapped duplicates, for one/two collections.
    if(c.catalogIndex>0)disc(row,24+shift,202-y,26,false);
    if(c.catalogIndex+1<c.catalogCount)disc(row,110+shift,202-y,26,false);
    disc(row,67+shift,195-y+(3-animationStep_)*2,28,true);
    arc(row,fonts,shortName_,67+shift,195+(3-animationStep_)*2,y);
}
bool LibraryVisual::render(M5GFX& display,M5Canvas& row,FontCache& fonts,const UiRenderContext& c,uint32_t now){
    if(!visible_)return false;
    CachedUiFont face(&fonts,12);
    if(clear_){display.fillRect(0,0,135,164,bg);clear_=false;return true;}
    if(!nameDone_){display.setFont(&face);display.setTextSize(1);display.setTextColor(TFT_WHITE,bg);
        // 97px keeps the P3A real long-name two-line regression meaningful.
        nameLayout=UiTextLayout::draw(display,c.catalogCount?c.libraryName:"暂无曲库",{19,130,97,32,2,3,true});
        display.setFont(&fonts::Font0);nameDone_=true;return true;}
    if(cover.state()!=previousState_){previousState_=cover.state();imageY_=0;}
    const uint8_t step=std::min<uint32_t>(3,(now-animationAt_)/40);
    if(step!=animationStep_ && bottomY_>=240){animationStep_=step;bottomY_=164;}
    if(bottomY_<240){
        const int height=std::min(18,240-bottomY_);row.clearClipRect();row.setClipRect(0,0,135,height);row.fillScreen(bg);
        drawDiscs(row,fonts,c,bottomY_,now);row.setFont(&face);row.setTextSize(1);row.setTextColor(accent,bg);
        char count[32];std::snprintf(count,sizeof(count),"%u/%u",unsigned(c.catalogCount?c.catalogIndex+1:0),unsigned(c.catalogCount));
        row.setCursor(4,211-bottomY_);row.print(count);
        row.setTextColor(TFT_WHITE,bg);row.setCursor(3,227-bottomY_);row.print(c.error&&c.error[0]?"加载失败 Enter重试":"左右选择 S设置");
        display.setClipRect(0,bottomY_,135,height);row.pushSprite(&display,0,bottomY_);display.clearClipRect();row.setFont(&fonts::Font0);row.clearClipRect();
        bottomY_+=height;if(bottomY_>=240)++frames;return true;
    }
    if(imageY_<120){
        const bool ready=cover.state()==MediaState::Ready;
        if(ready&&!cover.rowReady(imageY_)){cover.requestRow(imageY_);return false;}
        row.clearClipRect();row.setClipRect(0,0,135,2);row.fillScreen(bg);
        if(ready)cover.drawRow(row,imageY_);else{disc(row,67,60-imageY_,32,false);row.drawRect(7,-imageY_,120,120,muted);}
        display.setClipRect(0,6+imageY_,135,2);row.pushSprite(&display,0,6+imageY_);display.clearClipRect();row.clearClipRect();imageY_+=2;
        if(imageY_>=120&&ready)++coverFrames;return true;
    }
    return false;
}
} }
