#include "player/ui/PlaybackPageRoute.h"
#include "player/ui/DisplayPolicy.h"
#include "player/ui/media/CoverPolicy.h"
#include "player/ui/media/FrameCachePolicy.h"
#include "player/ui/media/MediaLayout.h"
using namespace adv_walkman::player;
static_assert(chooseMediaWork(0,true,true,true,true)==MediaWork::Font,"due glyphs have priority");
static_assert(chooseMediaWork(1,false,true,true,true)==MediaWork::Lyrics,"due text has priority");
static_assert(chooseMediaWork(3,true,true,true,true)==MediaWork::Cover,"cover still progresses during due work");
static_assert(playbackPageRoute(true,false,true)==PlaybackPageRoute::CurrentFolder,"Player Tab opens folder");
static_assert(playbackPageRoute(false,false,true)==PlaybackPageRoute::Player,"browser Tab returns without transport");
static_assert(playbackPageRoute(false,true,true)==PlaybackPageRoute::None,"Settings ignores Tab");
static_assert(playbackPageRoute(true,false,false)==PlaybackPageRoute::None,"no current track");
constexpr bool coverLifecycle(){
    for(uint8_t phase=1;phase<=3;++phase) {
#ifdef REPRODUCE_COVER_CLOSE_BUG
        const bool close=true;
#else
        const bool close=mayCloseCoverAfterFrame(phase);
#endif
        if(close)return false;
    }
    return mayCloseCoverAfterFrame(0);
}
static_assert(coverLifecycle(),"placeholder must not close validation file");
static_assert(validCoverDimensions(120,144,34560),"old cover remains supported");
static_assert(validCoverDimensions(135,135,36450),"full width square cover");
static_assert(!validCoverDimensions(136,135,36720),"reject wider than screen");
static_assert(!validCoverDimensions(135,135,36449),"reject truncated payload");
static_assert(coverRowsPerRead(135)==1&&coverRowsPerRead(120)==2,"bounded 512 byte reads");
static_assert(promoteFramePins(1)==2&&promoteFramePins(2)==0,"next becomes current; old current releases");
static_assert(promoteFramePins(7)==6&&promoteFramePins(4)==4,"UI pins survive promotion");
static_assert(currentTextSlot(0,1,1)==1&&currentTextSlot(2,1,2)==0,"advance reuses next text");
static_assert(currentTextSlot(2,1,90)==0,"seek replaces bounded slots");
constexpr bool wakeTab(){
    ScreenPowerController power;DisplayPreferences preferences;power.begin(0,true);
    power.sample(0,180000,true,preferences);
    if(!power.asleep())return false;
    constexpr uint64_t tab=uint64_t(1)<<14;
    power.sample(tab,180001,true,preferences);
    if(power.sample(tab,180026,true,preferences)||power.asleep())return false;
    if(power.sample(tab,180100,true,preferences))return false;
    power.sample(0,180101,true,preferences);power.sample(0,180126,true,preferences);
    power.sample(tab,180130,true,preferences);
    return power.sample(tab,180155,true,preferences)&&!power.swallowing();
}
static_assert(wakeTab(),"wake consumes whole Tab press, next press navigates");
