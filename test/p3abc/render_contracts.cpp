#include "player/ui/RenderContract.h"
#include <initializer_list>
#include "player/ui/WheelLayout.h"
#include "player/ui/DisplayLifecycle.h"
using namespace adv_walkman::player;
struct Injected {
    bool owned=false;
    unsigned chromeWrites=0,contentCalls=0;
    RenderStep result=RenderStep::Waiting;
    struct Content {Injected* p;constexpr RenderStep operator()()const{++p->contentCalls;p->owned=true;return p->result;}};
    struct Owned {Injected* p;constexpr bool operator()()const{return p->owned;}};
    constexpr RenderStep step(){
        const auto r=dispatchContent(owned,true,true,Content{this},Owned{this});
        if(r==RenderStep::Idle)++chromeWrites;
        return r;
    }
};
constexpr bool pendingNeverBorrows(){
    // Title, artist, time and progress are all pending. Starting the picture
    // read in THIS call must stop the same dispatch, not just the next call.
    for(unsigned dirty=0;dirty<4;++dirty){
        Injected p;
        if(p.step()!=RenderStep::Waiting||p.chromeWrites)return false;
        if(p.step()!=RenderStep::Waiting||p.chromeWrites)return false;
        p.result=RenderStep::Submitted;if(p.step()!=RenderStep::Submitted)return false;
        p.owned=false;p.result=RenderStep::Failed;
        if(p.step()!=RenderStep::Failed||p.chromeWrites)return false;
    }
    return true;
}
constexpr bool contiguousCommit(){
    for(unsigned extent:{188U,216U,174U,82U}){
        FrameCommit f;f.begin(7,19,0,extent,extent==82);
        if(f.complete()||f.submit(6,19,0,18)||f.submit(7,18,0,18)||f.submit(7,19,1,18))return false;
        unsigned y=0;while(y<extent){const auto h=extent-y<18?extent-y:18;
            if(!f.submit(7,19,y,h)||f.submit(7,19,y,h))return false;y+=h;}
        if(!f.complete())return false;f.cancel();if(f.complete()||f.submit(7,19,0,1))return false;
    }
    FrameCommit patch;patch.begin(2,3,40,122,true);
    return patch.partial&&!patch.submit(2,3,0,18)&&patch.submit(2,3,40,18);
}
static_assert(pendingNeverBorrows(),"owned pending stripe must not fall through into chrome");
static_assert(contiguousCommit(),"only matching contiguous accepted stripes complete the frame");
constexpr bool cancellationAtEveryBand(){
    // Exit, switch track, seek, sleep and failure share the same cancellation
    // boundary. Inject each one at every stripe, including first and last.
    for(unsigned cause=0;cause<5;++cause)for(unsigned cancelAt=0;cancelAt<12;++cancelAt){
        FrameCommit f;f.begin(11,1,0,216,false);
        for(unsigned n=0;n<cancelAt;++n)if(!f.submit(11,1,n*18,18))return false;
        DisplayLifecycle power;power.request(true);power.cancelled();
        f.cancel();power.request(false);power.drained();power.applied();
        if(f.complete()||f.submit(11,1,cancelAt*18,18)||power.pending()||power.asleep())return false;
        f.begin(12,2,0,188,false);
        if(f.submit(11,1,0,18)||!f.submit(12,2,0,18))return false;
    }
    return true;
}
constexpr bool wheelRules(){
    if(kLibraryNameTop+kLibraryNameHeight!=kLibraryWheelTop||kLibraryWheelTop!=196)return false;
    for(unsigned count=1;count<=7;++count)for(unsigned current=0;current<count;++current){
        if(wheelIndex(current,count,1)!=current)return false;
        for(unsigned slot=0;slot<3;++slot)if(wheelIndex(current,count,slot)>=count)return false;
        if(count<=2&&wheelIndex(current,count,0)!=wheelIndex(current,count,2))return false;
        if(count>=3&&wheelIndex(current,count,0)==wheelIndex(current,count,2))return false;
    }
    return wheelIndex(0,0,0)==0&&wheelIndex(0,3,0)==2&&wheelIndex(2,3,2)==0;
}
static_assert(cancellationAtEveryBand(),"cancelled frames never submit after page/track/power transitions");
static_assert(wheelRules(),"non-empty libraries have three mapped wheel slots with wrap");
static_assert(wheelAnimationStep(3,0)==3&&wheelAnimationStep(3,80)==3&&wheelAnimationStep(0,80)==1&&wheelAnimationStep(2,160)==3,"stationary wheel never restarts a completed animation");
static_assert(validStripeDestination(208,8)&&validStripeDestination(236,3)&&!validStripeDestination(236,18)&&!validStripeDestination(-1,18),"stripe submission stays inside its destination");
