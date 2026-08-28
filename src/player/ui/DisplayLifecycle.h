#pragma once
#include "DisplayPolicy.h"
namespace adv_walkman { namespace player {
// Desired state can change while handles are draining. Never restart or skip
// cancellation: only the final Apply uses the newest sleep/wake target.
class DisplayLifecycle {
 public:
    enum Stage:uint8_t {Idle,Cancel,Drain,Apply};
    ADV_DISPLAY_CX void request(bool asleep){desiredAsleep_=asleep;if(stage_==Idle)stage_=Cancel;}
    ADV_DISPLAY_CX void cancelled(){if(stage_==Cancel)stage_=Drain;}
    ADV_DISPLAY_CX void drained(){if(stage_==Drain)stage_=Apply;}
    ADV_DISPLAY_CX void applied(){if(stage_==Apply)stage_=Idle;}
    constexpr Stage stage()const{return stage_;}
    constexpr bool pending()const{return stage_!=Idle;}
    constexpr bool asleep()const{return desiredAsleep_;}
 private:Stage stage_=Idle;bool desiredAsleep_=false;
};
} }
