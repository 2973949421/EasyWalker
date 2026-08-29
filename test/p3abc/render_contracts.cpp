#include "player/ui/RenderContract.h"
#include <initializer_list>
#include "player/ui/WheelLayout.h"
#include "player/ui/DisplayLifecycle.h"
#include "player/ui/UiTransaction.h"
#include "player/ui/UiWorkScheduler.h"
#include "player/ui/LibraryPageController.h"
#include "player/p3abc/SaveTransaction.h"
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

constexpr bool libraryFrameRejectsStale(){
    const UiRequestToken current{4,9,12},old{4,8,11};
    LibraryFrameCommit frame;frame.begin(current);
    if(frame.mark(old,LibraryNameRegion)||frame.submitCover(old,0,18))return false;
    if(!frame.mark(current,LibraryNameRegion)||!frame.mark(current,LibraryWheelRegion))return false;
    unsigned y=0;while(y<174){const unsigned h=174-y<18?174-y:18;if(!frame.submitCover(current,y,h))return false;y+=h;}
    return frame.complete();
}
constexpr bool readyOldBandCannotBlockNewModel(){
    UiWorkSnapshot stuck{};stuck.library=true;stuck.browserReady=false;
    // The old band is active+ready, therefore neither waiting flag nor a font
    // read is set.  The browser model must still win immediately.
    return chooseUiWork(stuck,false,false)==UiScheduledWork::BrowserModel;
}
static_assert(libraryFrameRejectsStale(),"Library frame accepts only its exact request token");
static_assert(readyOldBandCannotBlockNewModel(),"ready old Library band must not block the latest browser model");

constexpr bool libraryTransactionTerminates(){
    LibraryTransaction tx;const UiRequestToken first{3,7,9},latest{3,8,10};
    tx.request(first,0);tx.validating(1);
    if(tx.stalled(2000)||!tx.stalled(2001))return false;
    if(!tx.beginRecovery(2001)||tx.state()!=LibraryPageState::Recovering)return false;
    // Retargeting starts a clean contract; no rows from the previous token
    // can make the latest request complete.
    tx.request(latest,2002);tx.rendering(2003);
    if(tx.submitCover(0,18,2004)==false)return false;
    LibraryFrameCommit stale;stale.begin(first);
    if(tx.state()!=LibraryPageState::Rendering||tx.publish(2004))return false;
    unsigned y=18;while(y<174){const unsigned h=174-y<18?174-y:18;
        if(!tx.submitCover(y,h,2005+y))return false;y+=h;}
    if(!tx.mark(LibraryNameRegion,2200)||!tx.mark(LibraryWheelRegion,2201)||!tx.publish(2202))return false;
    return tx.state()==LibraryPageState::Presented&&tx.displayed()==latest;
}

constexpr bool thousandRetargetsKeepLatest(){
    LibraryPageController page;UiRequestToken latest{};
    for(uint32_t i=1;i<=1000;++i){
        if(i%251==0)page.pageChanged();else page.requestSelection();latest=page.token();
        UiWorkSnapshot work{};work.library=true;work.browserReady=false;
        if(chooseUiWork(work,false,false)!=UiScheduledWork::BrowserModel)return false;
    }
    LibraryFrameCommit frame;frame.begin(latest);
    if(frame.mark({latest.pageEpoch,latest.requestGeneration-1,latest.resourceGeneration-1},LibraryNameRegion))return false;
    if(!frame.mark(latest,LibraryNameRegion)||!frame.mark(latest,LibraryWheelRegion))return false;
    for(unsigned y=0;y<174;){const unsigned h=174-y<18?174-y:18;if(!frame.submitCover(latest,y,h))return false;y+=h;}
    return frame.complete();
}

constexpr bool saveTicketsAreNeverLost(){
    SaveTransaction save;
    const auto first=save.request(10,2,3);if(first!=1||!save.begin(11))return false;
    const auto second=save.request(12,4,5);if(second!=2||!save.active()||!save.pending())return false;
    save.finish(SaveOutcome::Succeeded,20);if(save.completed()!=1||!save.needsNext()||!save.begin(21))return false;
    if(save.requiredPlayerRevision()!=4||save.requiredDisplayRevision()!=5)return false;
    save.timeout(10021);
    return save.completed()==2&&save.status()==SaveStatus::TimedOut&&!save.pending();
}
constexpr bool saveOutcomesAreDistinct(){
    SaveTransaction save;
    save.request(1,1,1);save.begin(2);save.finish(SaveOutcome::StateSavedLogFailed,3);
    if(save.status()!=SaveStatus::Failed||save.outcome()!=SaveOutcome::StateSavedLogFailed)return false;
    save.request(4,2,2);save.begin(5);save.finish(SaveOutcome::StateFailed,6);
    if(save.outcome()!=SaveOutcome::StateFailed)return false;
    save.request(7,3,3);save.begin(8);save.timeout(10008);
    return save.status()==SaveStatus::TimedOut&&save.outcome()==SaveOutcome::TimedOut;
}
static_assert(libraryTransactionTerminates(),"Library transaction must terminate on the latest exact token");
static_assert(thousandRetargetsKeepLatest(),"1000 rapid Library retargets must preserve only the latest token");
static_assert(saveTicketsAreNeverLost(),"every T ticket must complete or time out, including a trailing request");
static_assert(saveOutcomesAreDistinct(),"state success and diagnostic success have distinct outcomes");
