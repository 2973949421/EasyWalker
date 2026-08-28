#pragma once
#include <SD.h>
#include <M5GFX.h>
#include <algorithm>
#include "ResourceIo.h"
namespace adv_walkman { namespace player {
struct CachedGlyph {
    uint32_t offset=0;
    uint16_t codepoint=0,arena=0xFFFF;
    uint8_t age=0,width=0,height=0;
    int8_t dx=0,dy=0;
    uint8_t font=0;
    uint8_t state:2,pinned:3;
    uint8_t advance=0;
    CachedGlyph():state(0),pinned(0){}
};
static_assert(sizeof(CachedGlyph)==16,"compact glyph metrics");
struct FontCacheStats {
    uint32_t reads=0,bytes=0,hits=0,misses=0,missing=0,ioErrors=0;
    uint32_t serviceMaxUs=0,drawMisses=0,capacityErrors=0;
    uint32_t opens=0,indexPageHits=0,firstDrawCodepoint=0;
    uint8_t firstDrawFont=255;
};
class FontCache final {
  public:
    enum Face : uint8_t { Cjk12, Cjk14, Cjk16, Cjk18, Latin10, Latin12, Latin14,
        LibraryCjk12,LibraryCjk18,LibraryLatin14,LibraryLatin22,FaceCount };
    static constexpr unsigned kBitmapBytes=15*1024,kMetrics=200;
    bool begin();void release();~FontCache(){release();}
    enum Pin : uint8_t { Next=1, Current=2, Ui=4, All=7 };
    bool request(uint32_t cp,uint8_t font,uint8_t pin=0);
    bool requestMetric(uint32_t cp,uint8_t font);
    bool requestUiMetrics(const char* text,uint8_t pixels,bool library=false);
    uint8_t latinAdvance(uint32_t cp,uint8_t face=Latin14)const{return work_&&cp<256&&face>=Latin10&&face<=Latin14?(work_->latinAdvance[face-Latin10][cp]&15):8;}
    uint8_t verticalAdvance(uint32_t cp)const{const uint8_t v=work_&&cp<256?work_->latinAdvance[Latin14-Latin10][cp]:0x88;return std::max<uint8_t>(v>>4,v&15);}
    int textWidth(const char* text,uint8_t pixels)const;
    bool requestText(const char* text,uint8_t font);
    bool requestUiWindow(const char* text,uint8_t font,int startPx,int widthPx,float size,bool library=false);
    void service();
    const CachedGlyph* find(uint32_t cp,uint8_t font)const;
    const uint8_t* bitmap(const CachedGlyph& glyph)const;
    void draw(lgfx::LGFXBase& target,uint32_t cp,uint8_t font,int x,int y,uint16_t fg,uint16_t bg,bool clockwise=false)const;
    bool busy()const{return phase_!=0;}
    bool available()const{return work_!=nullptr;}
    void clearPins(uint8_t mask=All);void promotePins();void suspend();
    bool suspendOne(); // true only once all handles are closed
    void setPresenting(bool value){presenting_=value;}
    bool presenting()const{return presenting_;}
    static uint8_t fontForPixels(int px){return px<=12?0:(px<=14?1:2);}
    static uint8_t faceFor(uint32_t cp,int px){return cp<256?(px<=10?Latin10:px<=12?Latin12:Latin14):(px<=12?Cjk12:px<=14?Cjk14:px<=16?Cjk16:Cjk18);}
    static uint8_t libraryFace(uint32_t cp,int px){return cp<256?(px>14?LibraryLatin22:LibraryLatin14):(px>14?LibraryCjk18:LibraryCjk12);}
    static bool packedLatin(uint8_t face){return face>=Latin10&&face<=Latin14;}
    static uint8_t pixelsFor(uint8_t face){return face==Cjk18?18:face==Cjk16?16:face==Cjk14||face==Latin14?14:face==Latin10?10:12;}
    const FontCacheStats& stats()const{return stats_;}
    const ResourceFailure& failure()const{return failure_;}
    void failurePath(char* output,size_t capacity)const;
    static constexpr size_t workBytes(){return kBitmapBytes+sizeof(CachedGlyph)*kMetrics+768;}
  private:
    struct Work{CachedGlyph metrics[kMetrics];uint8_t bitmaps[kBitmapBytes];uint8_t latinAdvance[3][256]{};};
    Work* work_=nullptr;fs::File index_,fontFile_;
    uint8_t phase_=0,currentFont_=255,failureFont_=0;
    bool metricOnly_=false,presenting_=false,failureIndex_=true;
    int pending_=-1;uint16_t used_=0;uint8_t clock_=0;
    uint32_t count_=0,lo_=0,hi_=0,vlwSize_=0;
    ResourceFailure failure_{};mutable FontCacheStats stats_{};
    uint8_t indexPage_[512]{};
    struct FaceIndex {uint32_t count=0,vlwSize=0,headerCrc=0;uint8_t format=0;bool checked=false,paired=false;};
    FaceIndex indexes_[FaceCount]{};
    uint8_t indexFormat_=0;
    uint32_t indexPageAt_=UINT32_MAX;
    uint16_t indexPageSize_=0;
    void fail(bool io,const char* reason,const char* operation,int expected=-1,int actual=-1);
    bool allocateBitmap(CachedGlyph& glyph);void compact();uint8_t tick();
};
class CachedUiFont final:public lgfx::IFont {
  public:
    explicit CachedUiFont(FontCache* cache,uint8_t pixels=12,bool library=false):cache_(cache),pixels_(pixels),library_(library){}
    void getDefaultMetric(lgfx::FontMetrics* m)const override;
    bool updateFontMetric(lgfx::FontMetrics* m,uint16_t cp)const override;
    size_t drawChar(lgfx::LGFXBase* gfx,int32_t x,int32_t y,uint16_t cp,const lgfx::TextStyle* style,lgfx::FontMetrics* metrics,int32_t& filled)const override;
  private:FontCache* cache_;uint8_t pixels_;bool library_;
    uint8_t face(uint32_t cp)const{return library_?FontCache::libraryFace(cp,pixels_):FontCache::faceFor(cp,pixels_);}
};
static_assert(FontCache::kBitmapBytes<=24*1024,"bitmap cache budget");
static_assert(sizeof(CachedGlyph)*FontCache::kMetrics+768<=4*1024,"metric cache budget");
} }
