#pragma once
#include <SD.h>
#include <M5GFX.h>
#include "ResourceIo.h"
namespace adv_walkman { namespace player {
struct CachedGlyph {
    uint32_t offset=0;
    uint16_t codepoint=0,age=0,arena=0xFFFF;
    uint8_t width=0,height=0;
    int8_t dx=0,dy=0;
    uint8_t font:2,state:2,pinned:1,reserved:3;
    uint8_t unused=0;
    CachedGlyph():font(0),state(0),pinned(0),reserved(0){}
};
static_assert(sizeof(CachedGlyph)==16,"compact glyph metrics");
struct FontCacheStats {
    uint32_t reads=0,bytes=0,hits=0,misses=0,missing=0,ioErrors=0;
    uint32_t serviceMaxUs=0,drawMisses=0,capacityErrors=0;
};
class FontCache final {
  public:
    static constexpr unsigned kBitmapBytes=16*1024,kMetrics=240;
    bool begin();void release();~FontCache(){release();}
    bool request(uint32_t cp,uint8_t font,bool pin=false);
    bool requestMetric(uint32_t cp,uint8_t font);
    uint8_t latinAdvance(uint32_t cp)const{return work_&&cp<256?work_->latinAdvance[cp]:8;}
    bool requestText(const char* text,uint8_t font);
    bool requestUiWindow(const char* text,uint8_t font,int startPx,int widthPx,float size);
    void service();
    const CachedGlyph* find(uint32_t cp,uint8_t font)const;
    const uint8_t* bitmap(const CachedGlyph& glyph)const;
    void draw(lgfx::LGFXBase& target,uint32_t cp,uint8_t font,int x,int y,uint16_t fg,uint16_t bg,bool clockwise=false)const;
    bool busy()const{return phase_!=0;}
    bool available()const{return work_!=nullptr;}
    void clearPins();void setPresenting(bool value){presenting_=value;}
    bool presenting()const{return presenting_;}
    static uint8_t fontForPixels(int px){return px<=12?0:(px<=14?1:2);}
    const FontCacheStats& stats()const{return stats_;}
    const ResourceFailure& failure()const{return failure_;}
    void failurePath(char* output,size_t capacity)const;
    static constexpr size_t workBytes(){return kBitmapBytes+sizeof(CachedGlyph)*kMetrics+256;}
  private:
    struct Work{CachedGlyph metrics[kMetrics];uint8_t bitmaps[kBitmapBytes];uint8_t latinAdvance[256]{};};
    Work* work_=nullptr;fs::File index_,fontFile_;
    uint8_t phase_=0,currentFont_=255,failureFont_=0;
    bool metricOnly_=false,presenting_=false,failureIndex_=true;
    int pending_=-1;uint16_t used_=0,clock_=0;
    uint32_t count_=0,lo_=0,hi_=0,vlwSize_=0;
    ResourceFailure failure_{};mutable FontCacheStats stats_{};
    void fail(bool io,const char* reason,const char* operation,int expected=-1,int actual=-1);
    bool allocateBitmap(CachedGlyph& glyph);void compact();uint16_t tick();
};
class CachedUiFont final:public lgfx::IFont {
  public:
    explicit CachedUiFont(FontCache* cache,uint8_t face=0):cache_(cache),face_(face){}
    void getDefaultMetric(lgfx::FontMetrics* m)const override;
    bool updateFontMetric(lgfx::FontMetrics* m,uint16_t cp)const override;
    size_t drawChar(lgfx::LGFXBase* gfx,int32_t x,int32_t y,uint16_t cp,const lgfx::TextStyle* style,lgfx::FontMetrics* metrics,int32_t& filled)const override;
  private:FontCache* cache_;uint8_t face_;
};
static_assert(FontCache::kBitmapBytes<=24*1024,"bitmap cache budget");
static_assert(sizeof(CachedGlyph)*FontCache::kMetrics+256<=4*1024,"metric cache budget");
} }
