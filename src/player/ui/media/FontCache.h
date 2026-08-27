#pragma once
#include <SD.h>
#include <M5GFX.h>
#include "MediaTypes.h"

namespace adv_walkman { namespace player {
struct CachedGlyph {
    uint32_t codepoint=0, offset=0, age=0;
    uint16_t width=0, height=0;
    int16_t advance=0, dx=0, dy=0;
    uint8_t font=0, slot=255, state=0; // 1=metric, 2=bitmap, 3=missing
};
struct FontCacheStats {
    uint32_t reads=0, bytes=0, hits=0, misses=0, missing=0, ioErrors=0;
    uint32_t serviceMaxUs=0, drawMisses=0;
};
class FontCache final {
  public:
    // 17 KiB of the 24 KiB bitmap allowance; leave explicit headroom for the
    // four stdio/FS handles rather than counting only C++ object sizes.
    static constexpr unsigned kSlots=68, kGlyphBytes=256, kMetrics=96;
    static constexpr size_t kBitmapBytes=kSlots*kGlyphBytes;
    bool begin();
    void release();
    ~FontCache() { release(); }
    bool request(uint32_t cp, uint8_t font); // 0/1/2=CJK12/14/16, 3=Latin12
    bool requestMetric(uint32_t cp,uint8_t font);
    uint8_t latinAdvance(uint32_t cp) const { return work_ && cp<256 ? work_->latinAdvance[cp] : 8; }
    bool requestText(const char* text, uint8_t font);
    bool requestUiWindow(const char* text,uint8_t font,int startPx,int widthPx,float size);
    void service(); // One file operation/read <= 512 bytes.
    const CachedGlyph* find(uint32_t cp,uint8_t font) const;
    const uint8_t* bitmap(const CachedGlyph& glyph) const;
    void draw(lgfx::LGFXBase& target, uint32_t cp,uint8_t font,int x,int y,
              uint16_t foreground,uint16_t background,bool clockwise=false) const;
    bool busy() const { return phase_!=0; }
    bool available() const { return work_!=nullptr; }
    static uint8_t fontForPixels(int px) { return px<=12 ? 0 : (px<=14 ? 1 : 2); }
    const FontCacheStats& stats() const { return stats_; }
    static constexpr size_t workBytes() { return kBitmapBytes+sizeof(CachedGlyph)*kMetrics+256; }
  private:
    // Retain tiny Latin advances separately: measuring a long sentence must
    // not livelock when its distinct characters exceed the bitmap/metric LRU.
    struct Work { CachedGlyph metrics[kMetrics]; uint8_t bitmaps[kBitmapBytes]; uint8_t latinAdvance[256]; };
    Work* work_=nullptr;
    fs::File index_,fontFile_;
    uint8_t phase_=0, currentFont_=255;
    bool metricOnly_=false;
    int pending_=-1;
    uint32_t count_=0,lo_=0,hi_=0,vlwSize_=0,clock_=0;
    mutable FontCacheStats stats_{};
    void fail(bool io);
};

// Keeps Font0 ASCII metrics (P3A regression) but uses the prepared native SD
// CJK bitmap for non-ASCII. Both measuring and drawing are strictly RAM-only.
class CachedUiFont final : public lgfx::IFont {
  public:
    explicit CachedUiFont(FontCache* cache,uint8_t face=0) : cache_(cache),face_(face) {}
    void getDefaultMetric(lgfx::FontMetrics* m) const override;
    bool updateFontMetric(lgfx::FontMetrics* m,uint16_t cp) const override;
    size_t drawChar(lgfx::LGFXBase* gfx,int32_t x,int32_t y,uint16_t cp,
                   const lgfx::TextStyle* style,lgfx::FontMetrics* metrics,int32_t& filled) const override;
  private: FontCache* cache_;uint8_t face_;
};
static_assert(FontCache::kBitmapBytes<=24*1024,"bitmap cache budget");
static_assert(sizeof(CachedGlyph)*FontCache::kMetrics+256<=4*1024,"metric cache budget");
} }
