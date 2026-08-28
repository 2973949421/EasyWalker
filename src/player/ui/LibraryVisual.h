#pragma once
#include "LibraryCoverReader.h"
#include "PageRenderers.h"
#include "media/FontCache.h"
namespace adv_walkman { namespace player {
class LibraryVisual {
 public:
    void select(const char* path,int direction=0);
    void release(){cover.finishBand();visible_=false;closing_=true;}
    bool serviceSuspension(){if(!closing_)return false;cover.suspend();closing_=false;return true;}
    void invalidate(){cover.finishBand();imageY_=0;bottomY_=196;nameDone_=false;clear_=true;nameLayout={};}
    bool prepare(FontCache& fonts,const UiRenderContext& c);
    void service(){cover.service();}
    bool render(M5GFX& display,M5Canvas& row,FontCache& fonts,const UiRenderContext& c,uint32_t now);
    bool complete()const{return nameDone_&&bottomY_>=240&&imageY_>=174;}
    LibraryCoverReader cover;
    UiTextLayoutResult nameLayout{};
    uint32_t frames=0,coverFrames=0;
 private:
    bool visible_=false,clear_=true,nameDone_=false,closing_=false;
    int imageY_=0,bottomY_=196,wheelTop_=196,direction_=0;
    float animationFrom_=0;
    uint32_t animationAt_=0;uint8_t animationStep_=3;
    MediaState previousState_=MediaState::Idle;
    char shortName_[64]{};
    void drawDiscs(M5Canvas& row,FontCache& fonts,const UiRenderContext& c,int y,uint32_t now);
};
} }
