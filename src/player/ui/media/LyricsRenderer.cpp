#include "LyricsRenderer.h"
#include <algorithm>
#include <cstring>
namespace adv_walkman { namespace player {
namespace {constexpr int kTop=MediaLayout::top,kHeight=MediaLayout::lyricHeight,kPitch=MediaLayout::pitch,kCell=MediaLayout::cell;constexpr uint16_t kBg=0x0861;}
int LyricsRenderer::step(uint32_t cp,FontCache& fonts,bool& ready){
    if(cp>=0x100)return kCell;
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
            const int x=right-int(col-first)*kPitch-kCell;
            if(x<6 || x+kCell>129 || y+advance>kHeight){stats_.layoutError=true;return;}
            glyphs_[stats_.glyphs++]={cp,color,uint8_t(x),uint8_t(kTop+y)};
        }y+=advance;
    }
    stats_.invalidUtf8|=invalid;
}
bool LyricsRenderer::prepare(const LyricsTimeline& timeline,FontCache& fonts,uint32_t positionMs,int target){
    if(target==-999)target=timeline.current();
    stats_=LyricsLayoutStats{};stats_.intro=target<0;
    if(!timeline.windowReady())return false;
    bool ready=true;
    const int relative=target-timeline.current();
    const int current=stats_.intro?1:relative;
    const char* original=timeline.text(current,0);const char* chinese=timeline.text(current,1);
    if(!*original && !*chinese){original="间奏";}
    const unsigned left=columns(original,fonts,ready),right=columns(chinese,fonts,ready);
    if(!ready)return false;
    // Intro uses exactly the same complete bilingual layout, only dimmer.
    // Seven 14px columns with 2px gaps + a 6px bilingual gap fit 123px.
    unsigned rightSlots=right,leftSlots=left;
    if(left+right>7){
        if(!right){leftSlots=7;rightSlots=0;}else if(!left){rightSlots=7;leftSlots=0;}
        else if(right<=3){rightSlots=right;leftSlots=7-right;}else if(left<=3){leftSlots=left;rightSlots=7-left;}
        else {leftSlots=3;rightSlots=4;}
    }
    const unsigned leftPages=leftSlots?(left+leftSlots-1)/leftSlots:1;
    const unsigned rightPages=rightSlots?(right+rightSlots-1)/rightSlots:1;
    const unsigned pages=std::max(leftPages,rightPages);
    const uint32_t duration=std::max<uint32_t>(1,timeline.cueEnd(target)-timeline.cueStart(target));
    const uint32_t elapsed=positionMs>timeline.cueStart(target)?positionMs-timeline.cueStart(target):0;
    const unsigned page=std::min<unsigned>(pages-1,uint64_t(elapsed)*pages/duration);
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
    if(!stats_.intro && extra>=kPitch){
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
        preview(relative-1,rightEdge-width-2);preview(relative+1,129);
    }
    return ready;
}
bool LyricsRenderer::prepareFrame(FontCache& fonts){
    // One font group at a time. Pins accumulate while preparing and are held
    // until the whole frame has reached the display.
    for(uint8_t face=MediaLayout::cjkFace;face<=3;++face)for(unsigned i=0;i<stats_.glyphs;++i){
        const auto cp=glyphs_[i].cp;if((cp<0x100?3:MediaLayout::cjkFace)==face && !fonts.request(cp,face,true))return false;
    }
    return !fonts.busy() && !fonts.stats().missing && !fonts.stats().ioErrors && !fonts.stats().capacityErrors;
}
bool LyricsRenderer::prepareStripe(FontCache& fonts,int y,int height,int shift){
    for(unsigned i=0;i<stats_.glyphs;++i){const auto& g=glyphs_[i];if(g.y+20<=y || g.y>=y+height || g.x+shift+18<6 || g.x+shift>=129)continue;if(!fonts.request(g.cp,g.cp<0x100?3:MediaLayout::cjkFace))return false;}
    return true;
}
void LyricsRenderer::drawStripe(lgfx::LGFXBase& canvas,const FontCache& fonts,int y,int height,int shift) const {
    canvas.setClipRect(6,0,123,height);
    for(unsigned i=0;i<stats_.glyphs;++i){const auto& g=glyphs_[i];if(g.y+20<=y || g.y>=y+height || g.x+shift+18<6 || g.x+shift>=129)continue;
        const uint8_t face=g.cp<0x100?3:MediaLayout::cjkFace;
        int x=g.x+shift,py=g.y-y;
        // Commas/stops sit at the upper right; brackets/quotes turn with the
        // vertical writing direction. Source text is never rewritten here.
        if(smallVerticalPunctuation(g.cp))if(const auto* m=fonts.find(g.cp,face)){
            x+=kCell-m->width-m->dx;py-=m->dy;
        }
        fonts.draw(canvas,g.cp,face,x,py,g.color,kBg,g.cp<0x100||rotateVerticalPunctuation(g.cp));
    }
    canvas.clearClipRect();
}
} }
