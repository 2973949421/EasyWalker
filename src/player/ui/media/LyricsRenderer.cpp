#include "LyricsRenderer.h"
#include <algorithm>
#include <cstring>
namespace adv_walkman { namespace player {
namespace {constexpr int kTop=4,kHeight=160,kPitch=18;constexpr uint16_t kBg=0x0861;}
int LyricsRenderer::step(uint32_t cp,FontCache& fonts,bool& ready){
    if(cp>=0x100)return 16;
    if(!fonts.requestMetric(cp,3)){ready=false;return 8;}
    return std::max<int>(4,fonts.latinAdvance(cp));
}
unsigned LyricsRenderer::columns(const char* text,FontCache& fonts,bool& ready){
    if(!*text)return 0;unsigned cols=1;int y=0;bool invalid=false;
    while(*text){const uint32_t cp=mediaCodepoint(text,invalid);const int advance=step(cp,fonts,ready);if(y+advance>kHeight){++cols;y=0;}y+=advance;}
    stats_.invalidUtf8|=invalid;return cols;
}
void LyricsRenderer::place(const char* text,unsigned first,unsigned capacity,int right,uint16_t color,FontCache& fonts){
    unsigned col=0;int y=0;bool ready=true,invalid=false;
    while(*text){const uint32_t cp=mediaCodepoint(text,invalid);const int advance=step(cp,fonts,ready);if(y+advance>kHeight){++col;y=0;}
        if(col>=first && col<first+capacity){
            if(stats_.glyphs>=kMaxGlyphs){stats_.layoutError=true;return;}
            const int x=right-int(col-first)*kPitch-16;
            if(x<6 || x+16>129 || y+advance>160){stats_.layoutError=true;return;}
            glyphs_[stats_.glyphs++]={cp,color,uint8_t(x),uint8_t(kTop+y)};
        }y+=advance;
    }
    stats_.invalidUtf8|=invalid;
}
bool LyricsRenderer::prepare(const LyricsTimeline& timeline,FontCache& fonts,uint32_t positionMs){
    stats_=LyricsLayoutStats{};stats_.intro=timeline.current()<0;
    if(!timeline.windowReady())return false;
    bool ready=true;
    const int current=stats_.intro?1:0;
    const char* original=timeline.text(current,0);const char* chinese=timeline.text(current,1);
    if(!*original && !*chinese){original="间奏";}
    const unsigned left=columns(original,fonts,ready),right=columns(chinese,fonts,ready);
    if(!ready)return false;
    if(stats_.intro) {
        // Intro preview is deliberately dim and below the two-character
        // label. Full long first lines are shown once their timestamp starts.
        stats_.columns=0;
        const char* p=*chinese?chinese:original;bool invalid=false;int y=30;
        while(*p && y+16<=164) {const uint32_t cp=mediaCodepoint(p,invalid);glyphs_[stats_.glyphs++]={cp,0x8410,59,uint8_t(y)};y+=step(cp,fonts,ready);}
        stats_.invalidUtf8=invalid;return true;
    }
    // 6 columns (108px including gaps) plus bilingual gap 6px fit 123px.
    unsigned rightSlots=right,leftSlots=left;
    if(left+right>6){
        if(!right){leftSlots=6;rightSlots=0;}else if(!left){rightSlots=6;leftSlots=0;}
        else if(right<=3){rightSlots=right;leftSlots=6-right;}else if(left<=3){leftSlots=left;rightSlots=6-left;}
        else {leftSlots=rightSlots=3;}
    }
    const unsigned leftPages=leftSlots?(left+leftSlots-1)/leftSlots:1;
    const unsigned rightPages=rightSlots?(right+rightSlots-1)/rightSlots:1;
    const unsigned pages=std::max(leftPages,rightPages);
    const uint32_t duration=std::max<uint32_t>(1,timeline.endMs()-timeline.startMs());
    const uint32_t elapsed=positionMs>timeline.startMs()?positionMs-timeline.startMs():0;
    const unsigned page=stats_.intro?0:std::min<unsigned>(pages-1,uint64_t(elapsed)*pages/duration);
    stats_.pages=std::min<unsigned>(255,pages);stats_.page=page;
    stats_.columns=leftSlots+rightSlots;
    const int width=(leftSlots+rightSlots)*kPitch+(leftSlots&&rightSlots?6:0)-2;
    const int rightEdge=67+std::max(0,width)/2;
    const uint16_t color=stats_.intro?0x8410:0xFFFF;
    place(chinese,rightPages>1?std::min(page,rightPages-1)*rightSlots:0,rightSlots,rightEdge,color,fonts);
    place(original,leftPages>1?std::min(page,leftPages-1)*leftSlots:0,leftSlots,
          rightEdge-int(rightSlots)*kPitch-(rightSlots?6:0),color,fonts);
    // Preview is all-or-nothing and never allocates a partial glyph column.
    const int extra=(123-width)/2;
    if(!stats_.intro && extra>=18){
        auto preview=[&](int relative,int edge){
            bool prepared=true;
            const char* orig=timeline.text(relative,0);const char* zh=timeline.text(relative,1);
            const unsigned l=columns(orig,fonts,prepared),r=columns(zh,fonts,prepared);
            const int widthPx=int(l+r)*kPitch+(l&&r?6:0)-2;
            if(!prepared){ready=false;return;}
            // A neighboring bilingual group is shown only as a whole. Never
            // silently drop its translation merely to squeeze in a preview.
            if(l+r && widthPx<=extra){
                place(zh,0,r,edge,0x4208,fonts);
                place(orig,0,l,edge-int(r)*kPitch-(r?6:0),0x4208,fonts);
            }
        };
        preview(-1,rightEdge-width-2);preview(1,129);
    }
    return ready;
}
bool LyricsRenderer::prepareStripe(FontCache& fonts,int y,int height,int shift){
    for(unsigned i=0;i<stats_.glyphs;++i){const auto& g=glyphs_[i];if(g.y+20<=y || g.y>=y+height || g.x+shift+18<6 || g.x+shift>=129)continue;if(!fonts.request(g.cp,g.cp<0x100?3:2))return false;}
    if(stats_.intro && y<24 && !fonts.requestText("前奏",2))return false;
    return true;
}
void LyricsRenderer::drawStripe(lgfx::LGFXBase& canvas,const FontCache& fonts,int y,int height,int shift) const {
    canvas.setClipRect(6,0,123,height);
    for(unsigned i=0;i<stats_.glyphs;++i){const auto& g=glyphs_[i];if(g.y+20<=y || g.y>=y+height || g.x+shift+18<6 || g.x+shift>=129)continue;
        // During the slide, clip the stage, not the underlying screen. Static
        // layouts are checked before drawing and contain complete cells.
        fonts.draw(canvas,g.cp,g.cp<0x100?3:2,g.x+shift,g.y-y,g.color,kBg,g.cp<0x100);
    }
    if(stats_.intro && y<24){fonts.draw(canvas,0x524D,2,51,4-y,0xFBE0,kBg);fonts.draw(canvas,0x594F,2,69,4-y,0xFBE0,kBg);}
    canvas.clearClipRect();
}
} }
