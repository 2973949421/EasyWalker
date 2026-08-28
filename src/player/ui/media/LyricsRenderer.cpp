#include "LyricsRenderer.h"
#include "VerticalWords.h"
#include <algorithm>
#include <cstring>
namespace adv_walkman { namespace player {
namespace {constexpr int kTop=MediaLayout::top,kHeight=MediaLayout::lyricHeight,kPitch=MediaLayout::pitch,kCell=MediaLayout::cell;constexpr uint16_t kBg=0x0861;}
int LyricsRenderer::step(uint32_t cp,FontCache& fonts,bool& ready){
    if(cp>=0x100)return kCell;
    if(!fonts.requestMetric(cp,MediaLayout::latinFace)){ready=false;return 8;}
    return std::max<int>(4,fonts.verticalAdvance(cp));
}
int LyricsRenderer::advanceColumn(const char* start,uint32_t cp,bool& inWord,int& y,
                                  unsigned& column,FontCache& fonts,bool& ready){
    const bool word=englishWordChar(cp);
    const int whole=word&&!inWord?englishWordHeight(start,kHeight,
        [&](uint32_t c){return step(c,fonts,ready);}):0;
    const int advance=step(cp,fonts,ready);
    if(nextVerticalColumn(y,advance,whole,kHeight)){++column;y=0;}
    inWord=word;
    return advance;
}
unsigned LyricsRenderer::columns(const char* text,FontCache& fonts,bool& ready){
    if(!*text)return 0;unsigned cols=1;int y=0;bool invalid=false,inWord=false;
    while(*text){const char* start=text;const uint32_t cp=mediaCodepoint(text,invalid);
        const int advance=advanceColumn(start,cp,inWord,y,cols,fonts,ready);y+=advance;}
    stats_.invalidUtf8|=invalid;return cols;
}
void LyricsRenderer::place(const char* text,unsigned first,unsigned capacity,int right,uint16_t color,FontCache& fonts){
    unsigned col=0;int y=0;bool ready=true,invalid=false,inWord=false;
    while(*text){const char* start=text;const uint32_t cp=mediaCodepoint(text,invalid);
        const int advance=advanceColumn(start,cp,inWord,y,col,fonts,ready);
        if(col>=first && col<first+capacity){
            if(stats_.glyphs>=kMaxGlyphs){stats_.layoutError=true;return;}
            const int x=right-int(col-first)*kPitch-kCell;
            if(x<6 || x+kCell>129 || y+advance>kHeight){stats_.layoutError=true;return;}
            if(cp>0xFFFF){stats_.layoutError=true;return;}
            glyphs_[stats_.glyphs++]={uint16_t(cp),uint8_t(x),uint8_t(kTop+y)};
        }y+=advance;
    }
    stats_.invalidUtf8|=invalid;
}
bool LyricsRenderer::prepare(const LyricsTimeline& timeline,FontCache& fonts,uint32_t positionMs,int target){
    if(target==-999)target=timeline.current();
    stats_=LyricsLayoutStats{};stats_.intro=target<0;
    prepareCodepoint_=0;prepareFace_=0;
    if(!timeline.cueReady(std::max(0,target)))return false;
    bool ready=true;
    const int relative=std::max(0,target)-timeline.current();
    const int current=relative;
    const char* original=timeline.text(current,0);const char* chinese=timeline.text(current,1);
    if(!*original && !*chinese)return true;
    const unsigned left=columns(original,fonts,ready),right=columns(chinese,fonts,ready);
    if(!ready)return false;
    // Current bilingual cue only; continuation columns are all equally bright.
    unsigned rightSlots=right,leftSlots=left;
    if(left+right>MediaLayout::columns){
        if(!right){leftSlots=MediaLayout::columns;rightSlots=0;}else if(!left){rightSlots=MediaLayout::columns;leftSlots=0;}
        else if(right<=3){rightSlots=right;leftSlots=MediaLayout::columns-right;}else if(left<=3){leftSlots=left;rightSlots=MediaLayout::columns-left;}
        else {leftSlots=3;rightSlots=3;}
    }
    const unsigned leftPages=leftSlots?(left+leftSlots-1)/leftSlots:1;
    const unsigned rightPages=rightSlots?(right+rightSlots-1)/rightSlots:1;
    const unsigned pages=std::max(leftPages,rightPages);
    const uint32_t duration=std::max<uint32_t>(1,timeline.cueEnd(target)-timeline.cueStart(target));
    const uint32_t elapsed=positionMs>timeline.cueStart(target)?positionMs-timeline.cueStart(target):0;
    const unsigned page=stats_.intro?0:std::min<unsigned>(pages-1,uint64_t(elapsed)*pages/duration);
    stats_.pages=std::min<unsigned>(255,pages);stats_.page=page;
    stats_.columns=leftSlots+rightSlots;
    const int width=(leftSlots+rightSlots)*kPitch+(leftSlots&&rightSlots?6:0)-(kPitch-kCell);
    const int rightEdge=67+std::max(0,width)/2;
    const uint16_t color=stats_.intro?0x8410:0xFFFF;color_=color;
    place(chinese,rightPages>1?std::min(page,rightPages-1)*rightSlots:0,rightSlots,rightEdge,color,fonts);
    place(original,leftPages>1?std::min(page,leftPages-1)*leftSlots:0,leftSlots,
          rightEdge-int(rightSlots)*kPitch-(rightSlots?6:0),color,fonts);
    return ready;
}
bool LyricsRenderer::prepareFrame(FontCache& fonts,uint8_t pin){
    // One font group at a time. Pins accumulate while preparing and are held
    // until the whole frame has reached the display.
    // VLW bitmaps are ordered by codepoint. Dedupe/order without another frame
    // or bitmap buffer; at most kMaxGlyphs small comparisons per unique glyph.
    while(prepareFace_<2){
        const uint8_t face=prepareFace_==0?MediaLayout::cjkFace:MediaLayout::latinFace;
        uint32_t next=0;
        do{next=0x10000;
            for(unsigned i=0;i<stats_.glyphs;++i){const auto cp=glyphs_[i].cp;
                if(cp>=prepareCodepoint_&&(cp<0x100?MediaLayout::latinFace:MediaLayout::cjkFace)==face)next=std::min<uint32_t>(next,cp);}
            if(next==0x10000)break;
            if(!fonts.request(next,face,pin))return false;prepareCodepoint_=next+1;
        }while(prepareCodepoint_<0x10000);
        ++prepareFace_;prepareCodepoint_=0;
    }
    return !fonts.busy() && !fonts.stats().missing && !fonts.stats().ioErrors && !fonts.stats().capacityErrors;
}
bool LyricsRenderer::prepareStripe(FontCache& fonts,int y,int height,int shift){
    for(unsigned i=0;i<stats_.glyphs;++i){const auto& g=glyphs_[i];if(g.y+20<=y || g.y>=y+height || g.x+shift+18<6 || g.x+shift>=129)continue;if(!fonts.request(g.cp,g.cp<0x100?MediaLayout::latinFace:MediaLayout::cjkFace))return false;}
    return true;
}
void LyricsRenderer::drawStripe(lgfx::LGFXBase& canvas,const FontCache& fonts,int y,int height,int shift) const {
    canvas.setClipRect(6,0,123,height);
    for(unsigned i=0;i<stats_.glyphs;++i){const auto& g=glyphs_[i];if(g.y+20<=y || g.y>=y+height || g.x+shift+18<6 || g.x+shift>=129)continue;
        const uint8_t face=g.cp<0x100?MediaLayout::latinFace:MediaLayout::cjkFace;
        int x=g.x+shift,py=g.y-y;
        // Commas/stops sit at the upper right; brackets/quotes turn with the
        // vertical writing direction. Source text is never rewritten here.
        if(smallVerticalPunctuation(g.cp))if(const auto* m=fonts.find(g.cp,face)){
            x+=kCell-m->width-m->dx;py-=m->dy;
        }
        fonts.draw(canvas,g.cp,face,x,py,color_,kBg,g.cp<0x100||rotateVerticalPunctuation(g.cp));
    }
    canvas.clearClipRect();
}
} }
