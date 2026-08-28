#pragma once
#include "FontCache.h"
#include "LyricsTimeline.h"
#include "MediaLayout.h"
namespace adv_walkman { namespace player {
struct LyricsLayoutStats {
    uint16_t glyphs=0;uint8_t columns=0,pages=1,page=0;
    bool intro=false,invalidUtf8=false,layoutError=false;
};
class LyricsRenderer final {
  public:
    // Returns false when a metric needed for Latin layout is being fetched.
    bool prepare(const LyricsTimeline& timeline,FontCache& fonts,uint32_t positionMs,int target=-999);
    bool prepareFrame(FontCache& fonts,uint8_t pin=FontCache::Next);
    bool prepareStripe(FontCache& fonts,int y,int height,int shift);
    void drawStripe(lgfx::LGFXBase& canvas,const FontCache& fonts,int y,int height,int shift) const;
    const LyricsLayoutStats& stats() const {return stats_;}
  private:
    // Six columns, 174px high, with a minimum 4px Latin advance.
    // Stage-local coordinates fit in bytes; no font id is needed per glyph.
    struct Glyph {uint16_t cp;uint8_t x,y;};
    static_assert(sizeof(Glyph)==4,"two compact layouts replace one old layout");
    static constexpr unsigned kMaxGlyphs=MediaLayout::columns*(MediaLayout::lyricHeight/4);
    Glyph glyphs_[kMaxGlyphs]{};
    LyricsLayoutStats stats_{};
    uint16_t color_=0xFFFF;
    uint32_t prepareCodepoint_=0;
    uint8_t prepareFace_=0;
    int step(uint32_t cp,FontCache& font,bool& ready);
    int advanceColumn(const char* start,uint32_t cp,bool& inWord,int& y,
                      unsigned& column,FontCache& font,bool& ready);
    unsigned columns(const char* text,FontCache& font,bool& ready);
    void place(const char* text,unsigned firstColumn,unsigned capacity,int right,
               uint16_t color,FontCache& font);
};
} }
