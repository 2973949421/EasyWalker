#include "FontCache.h"
#include "FrameCachePolicy.h"
#include <algorithm>
#include <new>
#include <cstring>
#include <cstdio>
namespace adv_walkman { namespace player {
namespace {
const char* names[]={"cjk-12","cjk-14","cjk-16","cjk-18","latin-10","latin-12","latin-14","library-cjk-12","library-cjk-18","library-latin-14","library-latin-22"};
uint16_t rgb565(uint32_t c){return ((c>>8)&0xF800)|((c>>5)&0x07E0)|((c>>3)&31);}
uint16_t blend(uint16_t f,uint16_t b,uint8_t a){
    const unsigned r=(((f>>11)*a+(b>>11)*(255-a)+127)/255);
    const unsigned g=(((f>>5)&63)*a+((b>>5)&63)*(255-a)+127)/255;
    const unsigned v=(((f&31)*a+(b&31)*(255-a)+127)/255);return (r<<11)|(g<<5)|v;
}
}
bool FontCache::begin(){if(!work_)work_=new(std::nothrow) Work{};return work_!=nullptr;}
void FontCache::release(){index_.close();fontFile_.close();delete work_;work_=nullptr;phase_=0;currentFont_=255;pending_=-1;used_=clock_=0;presenting_=false;failure_=ResourceFailure{};stats_=FontCacheStats{};for(auto& i:indexes_)i={};}
uint8_t FontCache::tick(){if(++clock_==0){for(auto& g:work_->metrics)g.age/=2;clock_=128;}return clock_;}
const CachedGlyph* FontCache::find(uint32_t cp,uint8_t font)const{
    if(!work_ || cp>0xFFFF)return nullptr;for(const auto& g:work_->metrics)if(g.state && g.codepoint==cp && g.font==font)return &g;return nullptr;
}
const uint8_t* FontCache::bitmap(const CachedGlyph& g)const{return work_&&g.state==2&&g.arena<kBitmapBytes?work_->bitmaps+g.arena:nullptr;}
void FontCache::clearPins(uint8_t mask){if(mask==All)presenting_=false;if(work_)for(auto& g:work_->metrics)g.pinned&=~mask;}
void FontCache::promotePins(){if(work_)for(auto& g:work_->metrics)g.pinned=promoteFramePins(g.pinned);}
void FontCache::suspend(){index_.close();fontFile_.close();currentFont_=255;indexPageAt_=UINT32_MAX;if(busy())phase_=1;}
bool FontCache::suspendOne(){
    if(index_){index_.close();return false;}
    if(fontFile_){fontFile_.close();return false;}
    currentFont_=255;indexPageAt_=UINT32_MAX;if(busy())phase_=1;return true;
}
bool FontCache::request(uint32_t cp,uint8_t font,uint8_t pin){
    if(!work_ || font>=FaceCount)return false;
    if(cp>0xFFFF){failureFont_=font;failureIndex_=true;failure_.set("font_non_bmp","lookup");++stats_.missing;return true;}
    if(auto* old=const_cast<CachedGlyph*>(find(cp,font))){
        old->age=tick();old->pinned|=pin;
        if(old->state==2 || old->state==3){++stats_.hits;return true;}
        if(busy() || presenting_)return false;pending_=int(old-work_->metrics);metricOnly_=false;
        // A metric can outlive its evicted bitmap. Its offset belongs to its
        // own VLW file, never whichever font was opened most recently.
        phase_=currentFont_==font&&index_&&fontFile_?(old->offset?4:3):1;lo_=0;hi_=count_;return false;
    }
    if(busy() || presenting_)return false;int candidate=-1;
    for(unsigned i=0;i<kMetrics;++i){const auto& g=work_->metrics[i];if(!g.state){candidate=i;break;}
        if(!g.pinned && (candidate<0 || g.age<work_->metrics[candidate].age))candidate=i;}
    if(candidate<0){++stats_.capacityErrors;failure_.set("font_metric_capacity","allocate");return false;}
    auto& g=work_->metrics[candidate];g=CachedGlyph{};g.codepoint=cp;g.font=font;g.age=tick();g.state=1;g.pinned=pin;
    pending_=candidate;++stats_.misses;metricOnly_=false;phase_=currentFont_==font&&index_&&fontFile_?3:1;lo_=0;hi_=count_;return false;
}
bool FontCache::requestMetric(uint32_t cp,uint8_t font){
    if(work_&&font>=Latin10&&font<=Latin14&&cp<256&&work_->latinAdvance[font-Latin10][cp])return true;
    if(auto* g=const_cast<CachedGlyph*>(find(cp,font)))if(g->offset || g->state==3){g->age=tick();return true;}
    if(busy() || presenting_)return false;const bool ready=request(cp,font);if(!ready&&busy())metricOnly_=true;return ready;
}
bool FontCache::requestText(const char* text,uint8_t font){if(!text)return true;bool invalid=false;while(*text){const auto cp=mediaCodepoint(text,invalid);if(!request(cp,font))return false;}return true;}
bool FontCache::requestUiWindow(const char* text,uint8_t font,int startPx,int widthPx,float size,bool library){
    // pixels, not a Font0 scale. Metrics and drawing use the very same face.
    (void)size;if(!text)return true;bool invalid=false;int x=0;
    while(*text){const auto cp=mediaCodepoint(text,invalid);if(cp=='\n'){x=0;continue;}
        const auto face=library?libraryFace(cp,font):faceFor(cp,font);if(!requestMetric(cp,face))return false;
        const auto* g=find(cp,face);const int advance=packedLatin(face)?latinAdvance(cp,face):(g?g->advance:font);
        if(x+advance>=startPx&&x<=startPx+widthPx&&!request(cp,face,Ui))return false;
        x+=advance;if(x>startPx+widthPx)break;}return true;
}
int FontCache::textWidth(const char* text,uint8_t pixels)const{
    if(!text)return 0;int width=0;bool invalid=false;
    while(*text){const auto cp=mediaCodepoint(text,invalid);const auto face=faceFor(cp,pixels);const auto* g=find(cp,face);
        width+=cp<256?latinAdvance(cp,face):(g?g->advance:pixels);}
    return width;
}
void FontCache::compact(){
    uint16_t next=0;int old=-1;
    for(unsigned n=0;n<kMetrics;++n){CachedGlyph* found=nullptr;
        for(auto& g:work_->metrics)if(g.state==2 && g.width && g.height && g.arena!=0xFFFF && int(g.arena)>old && (!found||g.arena<found->arena))found=&g;
        if(!found)break;const int previous=found->arena;const size_t length=(found->width*found->height+1)/2;
        std::memmove(work_->bitmaps+next,work_->bitmaps+previous,length);found->arena=next;next+=length;old=previous;
    }used_=next;
}
bool FontCache::allocateBitmap(CachedGlyph& glyph){
    const unsigned length=(glyph.width*glyph.height+1)/2;if(!length){glyph.arena=0;return true;}if(used_+length>kBitmapBytes)compact();
    unsigned reclaim=0;
    while(used_+length>kBitmapBytes+reclaim){CachedGlyph* victim=nullptr;
        for(auto& g:work_->metrics)if(g.state==2&&!g.pinned&&(!victim||g.age<victim->age))victim=&g;
        if(!victim){++stats_.capacityErrors;return false;}reclaim+=(victim->width*victim->height+1)/2;victim->state=1;victim->arena=0xFFFF;}
    if(reclaim)compact();
    glyph.arena=used_;used_+=length;return true;
}
void FontCache::failurePath(char* output,size_t size)const{std::snprintf(output,size,"/ADVWalkman/fonts/%s.%s",names[failureFont_],failureIndex_?(indexFormat_==2?"idx2":"idx"):"vlw");}
void FontCache::fail(bool io,const char* reason,const char* operation,int expected,int actual){
    failure_.set(reason,operation,errno,expected,actual);failureFont_=pending_>=0?work_->metrics[pending_].font:0;failureIndex_=phase_!=4&&phase_!=5&&phase_!=6;
    if(pending_>=0){auto& g=work_->metrics[pending_];g.state=3;g.arena=0xFFFF;if(packedLatin(g.font)&&g.codepoint<256)work_->latinAdvance[g.font-Latin10][g.codepoint]=0x88;}
    if(io)++stats_.ioErrors;else++stats_.missing;phase_=0;pending_=-1;index_.close();fontFile_.close();currentFont_=255;
}
void FontCache::service(){
    if(!work_||!busy()||pending_<0||presenting_)return;const uint32_t start=micros();auto& g=work_->metrics[pending_];uint8_t b[32]{};char path[64];errno=0;
    if(phase_==1){
        if(index_){index_.close();return;}
        if(fontFile_){fontFile_.close();return;}
        indexPageAt_=UINT32_MAX;++stats_.opens;currentFont_=g.font;
        indexFormat_=indexes_[g.font].format==1?1:2;
        std::snprintf(path,sizeof(path),"/ADVWalkman/fonts/%s.%s",names[g.font],indexFormat_==2?"idx2":"idx");
        const auto opened=openResource(index_,path,512,failure_);
        if(opened==MediaState::Missing&&indexFormat_==2){indexes_[g.font].format=1;phase_=1;failure_=ResourceFailure{};}
        else if(opened!=MediaState::Loading)fail(true,"font_index_open","open");
        else if(indexes_[g.font].checked){count_=indexes_[g.font].count;vlwSize_=indexes_[g.font].vlwSize;lo_=0;hi_=count_;phase_=5;}
        else phase_=2;
    }else if(phase_==2){++stats_.reads;const int wanted=indexFormat_==2?20:16;const int n=index_.read(b,wanted);stats_.bytes+=n>0?n:0;
        if(n!=wanted)fail(true,"font_index_read","read",wanted,n);
        else if(std::memcmp(b,"FIDX",4)||mediaU16(b+4)!=indexFormat_||mediaU16(b+6)!=(indexFormat_==2?16:24))fail(true,"font_index_header","validate");
        else{count_=mediaU32(b+8);vlwSize_=mediaU32(b+12);lo_=0;hi_=count_;
            if(count_>65536||index_.size()!=(indexFormat_==2?512+count_*16:16+count_*24))fail(true,"font_index_length","validate");
            else{auto& cached=indexes_[g.font];cached.count=count_;cached.vlwSize=vlwSize_;cached.headerCrc=indexFormat_==2?mediaU32(b+16):0;cached.format=indexFormat_;cached.checked=true;phase_=5;}}
    }else if(phase_==5){++stats_.opens;std::snprintf(path,sizeof(path),"/ADVWalkman/fonts/%s.vlw",names[g.font]);const auto opened=openResource(fontFile_,path,256,failure_);
        if(opened!=MediaState::Loading)fail(true,"font_bitmap_open","open");else if(fontFile_.size()!=vlwSize_)fail(true,"font_bitmap_length","validate");else phase_=indexFormat_==2&&!indexes_[g.font].paired?7:3;
    }else if(phase_==7){++stats_.reads;const int n=fontFile_.read(b,24);stats_.bytes+=n>0?n:0;
        if(n!=24||(mediaCrc(~0U,b,24)^~0U)!=indexes_[g.font].headerCrc)fail(true,"font_index_pair","validate",24,n);
        else{indexes_[g.font].paired=true;phase_=3;}
    }else if(phase_==3){
        // At most four small index reads / 750us soft CPU slice.
        for(unsigned attempt=0;attempt<4 && phase_==3;++attempt){
            if(lo_>=hi_){fail(false,"font_glyph_missing","lookup");break;}
            const uint32_t middle=indexFormat_==2?g.codepoint:lo_+(hi_-lo_)/2;
            if(middle>=count_){fail(false,"font_glyph_missing","lookup");break;}
            // 21 whole records (504 bytes). A miss is one bounded I/O step;
            // the next service call searches RAM before requesting more I/O.
            const uint32_t page=indexFormat_==2?512+(middle/32)*512:16+(middle/21)*504;
            const uint32_t offset=indexFormat_==2?(middle%32)*16:(middle%21)*24;
            if(indexPageAt_!=page){
                if(index_.position()!=page){if(!index_.seek(page))fail(true,"font_index_seek","seek");break;}
                const unsigned wanted=std::min<uint32_t>(indexFormat_==2?512:504,index_.size()-page);
                ++stats_.reads;const int n=index_.read(indexPage_,wanted);stats_.bytes+=n>0?n:0;
                if(n!=int(wanted)){fail(true,"font_record_read","read",wanted,n);break;}
                indexPageAt_=page;indexPageSize_=n;break;
            }
            ++stats_.indexPageHits;
            if(offset+(indexFormat_==2?16:24)>indexPageSize_){fail(true,"font_record_bounds","validate");break;}
            if(indexFormat_==2){
                if(!mediaU32(indexPage_+offset)){fail(false,"font_glyph_missing","lookup");break;}
                b[0]=g.codepoint&255;b[1]=g.codepoint>>8;b[2]=b[3]=0;
                std::memcpy(b+4,indexPage_+offset,14);
            }else std::memcpy(b,indexPage_+offset,24);
            if(mediaU32(b)<g.codepoint)lo_=middle+1;else if(mediaU32(b)>g.codepoint)hi_=middle;
            else{const unsigned w=mediaU16(b+8),h=mediaU16(b+10);const int dx=int16_t(mediaU16(b+14)),dy=int16_t(mediaU16(b+16));g.offset=mediaU32(b+4);
                if(w>22||h>22||dx<-128||dx>127||dy<-128||dy>127||g.offset>vlwSize_||w*h>vlwSize_-g.offset){fail(true,"font_metric_bounds","validate");break;}
                g.width=w;g.height=h;g.dx=dx;g.dy=dy;g.advance=std::max<int>(1,std::min<int>(255,int16_t(mediaU16(b+12))));
                if(packedLatin(g.font)&&g.codepoint<256){if(w>15||g.advance>15){fail(true,"latin_metric_bounds","validate");break;}work_->latinAdvance[g.font-Latin10][g.codepoint]=(w<<4)|g.advance;}
                if(metricOnly_){phase_=0;pending_=-1;}else phase_=4;
            }if(micros()-start>=750)break;
        }
    }else if(phase_==4){
        if(currentFont_!=g.font || !fontFile_){phase_=1;}
        else if(fontFile_.position()!=g.offset){if(!fontFile_.seek(g.offset))fail(true,"font_bitmap_seek","seek");}
        else if(!allocateBitmap(g)){fail(true,"font_frame_capacity","allocate");}
        else phase_=6;
    }else if(phase_==6){const unsigned length=g.width*g.height;++stats_.reads;
            {uint8_t source[22*22];const int n=fontFile_.read(source,length);stats_.bytes+=n>0?n:0;
                if(n!=int(length))fail(true,"font_bitmap_read","read",length,n);
                else{for(unsigned i=0;i<length;i+=2){const unsigned a=(source[i]+8)/17,b=i+1<length?(source[i+1]+8)/17:0;work_->bitmaps[g.arena+i/2]=(a<<4)|b;}
                    g.state=2;phase_=0;pending_=-1;}}
    }stats_.serviceMaxUs=std::max<uint32_t>(stats_.serviceMaxUs,micros()-start);
}
void FontCache::draw(lgfx::LGFXBase& target,uint32_t cp,uint8_t font,int x,int y,uint16_t fg,uint16_t bg,bool clockwise)const{
    const auto* g=find(cp,font);const auto* pixels=g?bitmap(*g):nullptr;if(!pixels){if(!stats_.drawMisses){stats_.firstDrawCodepoint=cp;stats_.firstDrawFont=font;}++stats_.drawMisses;target.drawRect(x+2,y+2,8,10,TFT_ORANGE);return;}
    int cl,ct,cw,ch;target.getClipRect(&cl,&ct,&cw,&ch);
    // Intersect once, then visit only pixels in this stripe. Coverage is packed
    // on load; SD remains VLW 8-bit so old resources stay readable.
    const int i0=std::max(0,clockwise?ct-y:cl-x-g->dx),i1=std::min<int>(g->width,clockwise?ct+ch-y:cl+cw-x-g->dx);
    const int j0=std::max(0,clockwise?x+g->height-(cl+cw):ct-y-g->dy),j1=std::min<int>(g->height,clockwise?x+g->height-cl:ct+ch-y-g->dy);
    uint16_t palette[16];for(unsigned a=0;a<16;++a)palette[a]=blend(fg,bg,a*17);
    for(int j=j0;j<j1;++j)for(int i=i0;i<i1;){
        const unsigned p=j*g->width+i,a=(pixels[p/2]>>(p%2?0:4))&15;int end=i+1;
        while(end<i1){const unsigned q=j*g->width+end;if(((pixels[q/2]>>(q%2?0:4))&15)!=a)break;++end;}
        if(a){if(clockwise)target.drawFastVLine(x+g->height-1-j,y+i,end-i,palette[a]);
            else target.drawFastHLine(x+g->dx+i,y+g->dy+j,end-i,palette[a]);}
        i=end;
    }
}
void CachedUiFont::getDefaultMetric(lgfx::FontMetrics* m)const{*m={};m->height=m->y_advance=pixels_;m->baseline=pixels_;m->width=m->x_advance=pixels_;}
bool CachedUiFont::updateFontMetric(lgfx::FontMetrics* m,uint16_t cp)const{
    if(!cache_||!cache_->available())return fonts::Font0.updateFontMetric(m,cp);
    getDefaultMetric(m);const auto font=face(cp);const auto* g=cache_->find(cp,font);
    m->x_advance=FontCache::packedLatin(font)?cache_->latinAdvance(cp,font):(g?g->advance:pixels_);
    m->width=g?g->width:m->x_advance;m->x_offset=g?g->dx:0;return true;
}
size_t CachedUiFont::drawChar(lgfx::LGFXBase* gfx,int32_t x,int32_t y,uint16_t cp,const lgfx::TextStyle* style,lgfx::FontMetrics* metrics,int32_t& filled)const{
    if(!cache_||!cache_->available())return fonts::Font0.drawChar(gfx,x,y,cp,style,metrics,filled);
    const int advance=metrics->x_advance;const uint8_t font=face(cp);
    int cl,ct,cw,ch;gfx->getClipRect(&cl,&ct,&cw,&ch);
    if(x+pixels_<cl||x>=cl+cw||y+pixels_<=ct||y>=ct+ch)return advance;
    cache_->draw(*gfx,cp,font,x,y,rgb565(style->fore_rgb888),rgb565(style->back_rgb888));filled=x+advance;return advance;
}
} }
