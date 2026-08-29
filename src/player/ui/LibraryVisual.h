#pragma once
#include "LibraryCoverReader.h"
#include "PageRenderers.h"
#include "media/FontCache.h"
#include "WheelLayout.h"
#include "UiTransaction.h"
namespace adv_walkman { namespace player {
class LibraryVisual {
 public:
    void select(const char* path,int direction,UiRequestToken token);
    void release(){cover.finishBand();transaction_.cancel(millis());visible_=false;closing_=true;}
    bool serviceSuspension(){if(!closing_)return false;cover.suspend();closing_=false;return true;}
    void invalidate(){cover.finishBand();imageY_=cover.state()==MediaState::Ready?0:174;bottomY_=kLibraryWheelTop;nameDone_=false;nameMeasured_=false;clear_=true;nameLayout={};nameOffset_=0;nameEpoch_=nameTick_=millis();transaction_.rendering(millis());}
    void invalidateName(){nameDone_=false;nameTick_=0;}
    void cancelPending(){cover.cancelPending();transaction_.cancel(millis());}
    void showError(){cover.cancelPending();transaction_.fail(millis());nameDone_=false;nameMeasured_=false;}
    void setShortName(unsigned slot,const char* text);
    uint8_t shortNamesReady()const{return namesReady_;}
    bool prepare(FontCache& fonts,const UiRenderContext& c);
    void service(){cover.service();}
    bool render(M5GFX& display,M5Canvas& row,FontCache& fonts,const UiRenderContext& c,uint32_t now);
    bool complete()const{return transaction_.state()==LibraryPageState::Presented||transaction_.state()==LibraryPageState::Error;}
    const LibraryTransaction& transaction()const{return transaction_;}
    uint32_t stalls=0,recoveries=0,failures=0;
    LibraryCoverReader cover;
    UiTextLayoutResult nameLayout{};
    uint32_t frames=0,coverFrames=0;
 private:
    bool visible_=false,clear_=true,nameDone_=false,closing_=false;
    int imageY_=0,bottomY_=196;
    float animationFrom_=0;
    uint32_t animationAt_=0;uint8_t animationStep_=3;
    MediaState previousState_=MediaState::Idle;
    char shortNames_[3][44]{};
    uint8_t namesReady_=0;
    bool nameMeasured_=false;
    int16_t nameLeft_=0,nameWidth_=0,nameOffset_=0;
    uint32_t nameEpoch_=0,nameTick_=0;
    LibraryTransaction transaction_{};
    bool drawName(M5GFX& display,FontCache& fonts,const UiRenderContext& c,uint32_t now);
    void drawDiscs(M5Canvas& row,FontCache& fonts,const UiRenderContext& c,int y,uint32_t now);
};
} }
