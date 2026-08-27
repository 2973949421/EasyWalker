#include "FontCache.h"
#include <algorithm>
#include <new>
#include <cstring>
#include <cstdio>

namespace adv_walkman { namespace player {
namespace {
const char* names[]={"cjk-12","cjk-14","cjk-16","latin-12"};
uint16_t rgb565(uint32_t c) { return ((c>>8)&0xF800)|((c>>5)&0x07E0)|((c>>3)&31); }
uint16_t blend(uint16_t f,uint16_t b,uint8_t a) {
    const unsigned r=(((f>>11)*a+(b>>11)*(255-a)+127)/255);
    const unsigned g=((((f>>5)&63)*a+((b>>5)&63)*(255-a)+127)/255);
    const unsigned v=(((f&31)*a+(b&31)*(255-a)+127)/255);
    return (r<<11)|(g<<5)|v;
}
}
bool FontCache::begin() { if (!work_) work_=new(std::nothrow) Work{}; return work_!=nullptr; }
void FontCache::release() { index_.close();fontFile_.close();delete work_;work_=nullptr;phase_=0;currentFont_=255;pending_=-1; }
const CachedGlyph* FontCache::find(uint32_t cp,uint8_t font) const {
    if (!work_) return nullptr;
    for (const auto& g:work_->metrics) if (g.state && g.codepoint==cp && g.font==font) return &g;
    return nullptr;
}
const uint8_t* FontCache::bitmap(const CachedGlyph& g) const {
    return work_ && g.state==2 && g.slot<kSlots ? work_->bitmaps+g.slot*kGlyphBytes : nullptr;
}
bool FontCache::request(uint32_t cp,uint8_t font) {
    if (!work_ || font>3) return false;
    if (auto* old=const_cast<CachedGlyph*>(find(cp,font))) {
        old->age=++clock_;
        if (old->state==2 || old->state==3) { ++stats_.hits; return true; }
        if (busy()) return false;
        pending_=int(old-work_->metrics);phase_=4;metricOnly_=false;return false;
    }
    if (busy()) return false;
    unsigned candidate=0;
    for (unsigned i=0;i<kMetrics;++i) {
        if (!work_->metrics[i].state) { candidate=i;break; }
        if (work_->metrics[i].age<work_->metrics[candidate].age) candidate=i;
    }
    auto& g=work_->metrics[candidate];g=CachedGlyph{};g.codepoint=cp;g.font=font;g.age=++clock_;g.state=1;
    pending_=candidate;++stats_.misses;metricOnly_=false;
    phase_=currentFont_==font && index_ && fontFile_ ? 3 : 1;
    lo_=0;hi_=count_;return false;
}
bool FontCache::requestMetric(uint32_t cp,uint8_t font) {
    if(work_ && font==3 && cp<256 && work_->latinAdvance[cp])return true;
    if(auto* g=const_cast<CachedGlyph*>(find(cp,font))) {
        if(g->offset || g->state==3){g->age=++clock_;return true;}
    }
    if(busy())return false;
    const bool ready=request(cp,font);if(!ready && busy())metricOnly_=true;return ready;
}
bool FontCache::requestText(const char* text,uint8_t font) {
    if (!text) return true;
    bool invalid=false;
    while (*text) { const uint32_t cp=mediaCodepoint(text,invalid);if(cp>=0x100 && !request(cp,font)) return false; }
    return true;
}
bool FontCache::requestUiWindow(const char* text,uint8_t font,int startPx,int widthPx,float size) {
    if(!text)return true;bool invalid=false;float x=0;
    while(*text) {
        const uint32_t cp=mediaCodepoint(text,invalid);const float advance=(cp>=0x100?8:6)*size;
        if(x+advance>=startPx && x<=startPx+widthPx && cp>=0x100 && !request(cp,font))return false;
        x+=advance;if(x>startPx+widthPx)break;
    }return true;
}
void FontCache::fail(bool io) {
    if (pending_>=0) { auto& g=work_->metrics[pending_];g.state=3;g.slot=255;
        if(g.font==3 && g.codepoint<256)work_->latinAdvance[g.codepoint]=8;
    }
    if(io)++stats_.ioErrors;else++stats_.missing;
    phase_=0;pending_=-1;
}
void FontCache::service() {
    if (!work_ || !busy() || pending_<0) return;
    const uint32_t start=micros();auto& g=work_->metrics[pending_];uint8_t b[32]{};char path[64];
    if(phase_==1) {
        index_.close();fontFile_.close();currentFont_=g.font;
        std::snprintf(path,sizeof(path),"/ADVWalkman/fonts/%s.idx",names[g.font]);
        index_=SD.open(path,FILE_READ);phase_=2;
        if(!index_ || !index_.setBufferSize(128))fail(true);
    } else if(phase_==2) {
        ++stats_.reads;stats_.bytes+=16;
        if(index_.read(b,16)!=16 || std::memcmp(b,"FIDX",4) || mediaU16(b+4)!=1 || mediaU16(b+6)!=24) fail(true);
        else {
            count_=mediaU32(b+8);vlwSize_=mediaU32(b+12);lo_=0;hi_=count_;
            if(count_>65536 || index_.size()!=16+count_*24) fail(true);else phase_=5;
        }
    } else if(phase_==5) {
        std::snprintf(path,sizeof(path),"/ADVWalkman/fonts/%s.vlw",names[g.font]);
        fontFile_=SD.open(path,FILE_READ);
        if(!fontFile_ || !fontFile_.setBufferSize(256) || fontFile_.size()!=vlwSize_)fail(true);else phase_=3;
    } else if(phase_==3) {
        if(lo_>=hi_) fail(false);
        else {
            const uint32_t middle=lo_+(hi_-lo_)/2;
            ++stats_.reads;stats_.bytes+=24;
            if(!index_.seek(16+middle*24) || index_.read(b,24)!=24) fail(true);
            else if(mediaU32(b)<g.codepoint)lo_=middle+1;
            else if(mediaU32(b)>g.codepoint)hi_=middle;
            else {
                g.offset=mediaU32(b+4);g.width=mediaU16(b+8);g.height=mediaU16(b+10);
                g.advance=int16_t(mediaU16(b+12));g.dx=int16_t(mediaU16(b+14));g.dy=int16_t(mediaU16(b+16));
                if(g.width*g.height>kGlyphBytes || g.offset>vlwSize_ || g.width*g.height>vlwSize_-g.offset)fail(true);
                else {
                    if(g.font==3 && g.codepoint<256)work_->latinAdvance[g.codepoint]=std::min<int>(255,std::max<int>(4,std::max<int>(g.width,g.advance)));
                    if(metricOnly_){phase_=0;pending_=-1;}else phase_=4;
                }
            }
        }
    } else if(phase_==4) {
        // A metric can survive bitmap eviction. Reopen the right file before
        // loading it; never read a same-offset bitmap from another font.
        if(currentFont_!=g.font) { phase_=1; }
        else {
            bool used[kSlots]{};for(const auto& item:work_->metrics)if(item.slot<kSlots)used[item.slot]=true;
            unsigned slot=0;while(slot<kSlots && used[slot])++slot;
            if(slot==kSlots) {
                CachedGlyph* victim=nullptr;
                for(auto& item:work_->metrics)if(item.slot<kSlots && (!victim || item.age<victim->age))victim=&item;
                slot=victim->slot;victim->slot=255;victim->state=1;
            }
            const size_t length=g.width*g.height;++stats_.reads;stats_.bytes+=length;
            if(!fontFile_.seek(g.offset) || fontFile_.read(work_->bitmaps+slot*kGlyphBytes,length)!=int(length))fail(true);
            else { g.slot=slot;g.state=2;phase_=0;pending_=-1; }
        }
    }
    stats_.serviceMaxUs=std::max<uint32_t>(stats_.serviceMaxUs,micros()-start);
}
void FontCache::draw(lgfx::LGFXBase& target,uint32_t cp,uint8_t font,int x,int y,
                     uint16_t fg,uint16_t bg,bool clockwise) const {
    const auto* g=find(cp,font);const auto* pixels=g?bitmap(*g):nullptr;
    if(!pixels) { ++stats_.drawMisses;target.drawRect(x+2,y+2,8,10,TFT_ORANGE);return; }
    int cl,ct,cw,ch;target.getClipRect(&cl,&ct,&cw,&ch);
    for(unsigned j=0;j<g->height;++j) for(unsigned i=0;i<g->width;++i) {
        const int px=clockwise ? x+g->height-1-j : x+g->dx+i;
        const int py=clockwise ? y+i : y+g->dy+j;
        if(px<cl || px>=cl+cw || py<ct || py>=ct+ch)continue;
        const uint8_t alpha=pixels[j*g->width+i];if(alpha)target.drawPixel(px,py,blend(fg,bg,alpha));
    }
}
void CachedUiFont::getDefaultMetric(lgfx::FontMetrics* m) const { fonts::Font0.getDefaultMetric(m); }
bool CachedUiFont::updateFontMetric(lgfx::FontMetrics* m,uint16_t cp) const {
    if(cp<0x100)return fonts::Font0.updateFontMetric(m,cp);
    // Native CJK em box: eight logical pixels, scaled by the existing UI size.
    getDefaultMetric(m);m->width=m->x_advance=8;m->x_offset=0;
    // CJK is a fixed em-cell font. SD glyphs are centered within that native
    // cell; metrics cannot change when an off-screen bitmap is evicted.
    return true;
}
size_t CachedUiFont::drawChar(lgfx::LGFXBase* gfx,int32_t x,int32_t y,uint16_t cp,
                             const lgfx::TextStyle* style,lgfx::FontMetrics* metrics,int32_t& filled) const {
    if(cp<0x100)return fonts::Font0.drawChar(gfx,x,y,cp,style,metrics,filled);
    const int advance=int(8*style->size_x);const uint8_t font=FontCache::fontForPixels(int(8*style->size_y+0.5f));
    if(cache_)cache_->draw(*gfx,cp,font,x,y,rgb565(style->fore_rgb888),rgb565(style->back_rgb888));
    filled=x+advance;return advance;
}
} }
