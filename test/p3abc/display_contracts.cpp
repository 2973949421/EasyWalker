#include "player/ui/DisplayPolicy.h"
#include "player/ui/DisplayLifecycle.h"
using namespace adv_walkman::player;
constexpr bool normalTimers(){
    DisplayPreferences p;ScreenPowerController s;s.begin(0,true);
    if(!s.sample(0,179999,true,p))return false;
    if(s.sample(0,180000,true,p)||s.sleeps!=1)return false;
    if(s.sample(1,180001,true,p)||s.sample(1,180026,true,p)||s.wakes!=1)return false;
    if(s.sample(1,200000,true,p))return false;
    s.sample(0,200001,true,p);if(!s.sample(0,200026,true,p))return false;
    if(!s.sample(0,350000,true,p))return false; // No old five-second wake timer.
    if(s.sample(0,380001,true,p))return false;
    return true;
}
constexpr bool chordsAndPages(){
    DisplayPreferences p;ScreenPowerController s;s.begin(100,false);
    if(s.sample(0,30100,false,p))return false;
    s.sample(3,30101,false,p);if(s.sample(3,30126,false,p)||s.wakes!=1)return false;
    s.sample(2,30130,false,p);if(s.sample(2,30155,false,p))return false;
    s.sample(0,30200,false,p);if(!s.sample(0,30225,false,p))return false;
    if(!s.sample(4,30250,false,p)||!s.sample(4,30275,false,p))return false;
    s.sample(0,30300,false,p);s.sample(0,30325,false,p);
    if(!s.sample(0,40000,true,p)||!s.sample(0,219999,true,p))return false;
    return !s.sample(0,220000,true,p);
}
constexpr bool wrapAndNever(){
    DisplayPreferences p;p.otherTimeout=0;ScreenPowerController s;s.begin(0xFFFFF000,false);
    if(!s.sample(0,uint32_t(0xFFFFF000U+14999),false,p))return false;
    if(s.sample(0,uint32_t(0xFFFFF000U+15000),false,p))return false;
    ScreenPowerController n;p.playerTimeout=6;n.begin(0,true);
    return n.sample(0,4000000000U,true,p);
}
static_assert(normalTimers(),"normal timeout and swallowed wake");
static_assert(chordsAndPages(),"whole chord and page timer");
static_assert(wrapAndNever(),"wrap and never sleep");
constexpr bool shortWake(){
    DisplayPreferences p;p.otherTimeout=0;ScreenPowerController s;s.begin(0,false);
    s.sample(0,15000,false,p);
    if(s.sample(1,15001,false,p)||s.asleep()||s.wakes!=1)return false;
    if(s.sample(0,15004,false,p)||s.sample(0,15028,false,p))return false;
    if(!s.sample(0,15029,false,p))return false;
    s.pageChanged(20000,false);
    return s.sample(0,34999,false,p)&&!s.sample(0,35000,false,p);
}
static_assert(shortWake(),"captured short wake survives debounce and restarts same-class page timer");
constexpr bool longWakeRelease(){
    DisplayPreferences p;p.otherTimeout=0;ScreenPowerController s;s.begin(0,false);
    s.sample(0,15000,false,p);s.sample(1,15001,false,p);s.sample(1,15026,false,p);
    s.sample(1,40000,false,p);s.sample(0,40001,false,p);
    return s.sample(0,40026,false,p)&&s.sample(0,55000,false,p)&&!s.sample(0,55001,false,p);
}
static_assert(longWakeRelease(),"held wake key release starts a full normal timeout");
constexpr bool interruptedCleanup(){
    // Inject sleep/wake at every resource-drain boundary. Production policy
    // must neither skip Drain nor restart it under rapid state changes.
    for(unsigned operation=0;operation<8;++operation){
        DisplayLifecycle life;life.request(true);if(life.stage()!=DisplayLifecycle::Cancel)return false;
        life.cancelled();
        for(unsigned i=0;i<8;++i){if(i==operation)life.request(false);if(life.stage()!=DisplayLifecycle::Drain)return false;}
        life.drained();if(life.asleep()||life.stage()!=DisplayLifecycle::Apply)return false;
        life.applied();if(life.pending())return false;
        life.request(true);life.request(false);if(life.stage()!=DisplayLifecycle::Cancel)return false;
    }
    return true;
}
static_assert(interruptedCleanup(),"wake during any pending resource close preserves cancellation ordering");
static_assert(validDisplayPreferences(DisplayPreferences{}),"defaults valid");
static_assert(displayTimeoutMs(3)==180000&&displayTimeoutMs(1)==30000,"page defaults");
constexpr bool recordChecks(){
    uint8_t b[24]={'D','S','P','L',1,0,24,0,7,0,0,0,4,0,0,0,70,3,1,0,0,0,0,0};
    if(validDisplayRecord(b))return false; // CRC absent.
    const auto crc=displayRecordCrc(b);for(unsigned i=0;i<4;++i)b[20+i]=crc>>(i*8);
    if(!validDisplayRecord(b))return false;
    b[16]=0;if(validDisplayRecord(b))return false;b[16]=70;
    b[4]=2;if(validDisplayRecord(b))return false;b[4]=1;
    b[20]^=1;if(validDisplayRecord(b))return false;
    return newerDisplayGeneration(8,7)&&!newerDisplayGeneration(7,8)&&newerDisplayGeneration(0,0xFFFFFFFFU);
}
static_assert(recordChecks(),"production settings CRC version range and slot generation");
#ifdef REPRODUCE_OLD_WAKE_TIMER
static_assert(displayTimeoutMs(3)==5000,"old five-second timer is forbidden");
#endif
