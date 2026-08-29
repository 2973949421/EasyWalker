#pragma once

#include <cstdint>

namespace adv_walkman { namespace player {

#if __cplusplus >= 201402L
#define ADV_SAVE_CONSTEXPR constexpr
#else
#define ADV_SAVE_CONSTEXPR inline
#endif

enum class SaveStatus : uint8_t {
    Idle,
    Requested,
    WaitingPlayer,
    WaitingDisplay,
    WritingDiagnostics,
    Verifying,
    Succeeded,
    Failed,
    TimedOut,
};

class SaveTransaction final {
 public:
    ADV_SAVE_CONSTEXPR uint32_t request(uint32_t now,uint32_t playerRevision,uint32_t displayRevision){
        ++requested_;if(!requested_)++requested_;
        latestPlayerRevision_=playerRevision;latestDisplayRevision_=displayRevision;
        requestedAt_=now;if(!active_)status_=SaveStatus::Requested;return requested_;
    }
    ADV_SAVE_CONSTEXPR bool begin(uint32_t now){
        if(active_||requested_==completed_)return false;
        active_=true;activeThrough_=requested_;requiredPlayerRevision_=latestPlayerRevision_;
        requiredDisplayRevision_=latestDisplayRevision_;startedAt_=now;status_=SaveStatus::WaitingPlayer;return true;
    }
    ADV_SAVE_CONSTEXPR void stage(SaveStatus value){if(active_)status_=value;}
    constexpr bool timedOut(uint32_t now)const{return active_&&uint32_t(now-startedAt_)>=10000;}
    ADV_SAVE_CONSTEXPR void finish(bool success,uint32_t now){
        if(!active_)return;active_=false;completed_=activeThrough_;completedAt_=now;
        status_=success?SaveStatus::Succeeded:SaveStatus::Failed;
    }
    ADV_SAVE_CONSTEXPR void timeout(uint32_t now){if(!active_)return;active_=false;completed_=activeThrough_;completedAt_=now;status_=SaveStatus::TimedOut;}
    constexpr bool pending()const{return active_||requested_!=completed_;}
    constexpr bool active()const{return active_;}
    constexpr bool needsNext()const{return !active_&&requested_!=completed_;}
    constexpr uint32_t requested()const{return requested_;}
    constexpr uint32_t activeThrough()const{return activeThrough_;}
    constexpr uint32_t completed()const{return completed_;}
    constexpr uint32_t requiredPlayerRevision()const{return requiredPlayerRevision_;}
    constexpr uint32_t requiredDisplayRevision()const{return requiredDisplayRevision_;}
    constexpr uint32_t startedAt()const{return startedAt_;}
    constexpr uint32_t completedAt()const{return completedAt_;}
    constexpr SaveStatus status()const{return status_;}
 private:
    uint32_t requested_=0,activeThrough_=0,completed_=0;
    uint32_t latestPlayerRevision_=0,latestDisplayRevision_=0;
    uint32_t requiredPlayerRevision_=0,requiredDisplayRevision_=0;
    uint32_t requestedAt_=0,startedAt_=0,completedAt_=0;
    SaveStatus status_=SaveStatus::Idle;
    bool active_=false;
};

// Product-level owner for the joint Player/Display/diagnostic checkpoint.
// FreeSession performs the bounded I/O, while this object is the sole source
// of ticket ordering and terminal status.
class CheckpointCoordinator final {
 public:
    uint32_t request(uint32_t now,uint32_t playerRevision,uint32_t displayRevision){return transaction_.request(now,playerRevision,displayRevision);}
    bool begin(uint32_t now){return transaction_.begin(now);}
    void stage(SaveStatus value){transaction_.stage(value);}
    bool timedOut(uint32_t now)const{return transaction_.timedOut(now);}
    void finish(bool success,uint32_t now){transaction_.finish(success,now);}
    void timeout(uint32_t now){transaction_.timeout(now);}
    bool pending()const{return transaction_.pending();}
    bool active()const{return transaction_.active();}
    bool needsNext()const{return transaction_.needsNext();}
    uint32_t requested()const{return transaction_.requested();}
    uint32_t activeThrough()const{return transaction_.activeThrough();}
    uint32_t completed()const{return transaction_.completed();}
    uint32_t requiredPlayerRevision()const{return transaction_.requiredPlayerRevision();}
    uint32_t requiredDisplayRevision()const{return transaction_.requiredDisplayRevision();}
    SaveStatus status()const{return transaction_.status();}
    const SaveTransaction& transaction()const{return transaction_;}
 private:
    SaveTransaction transaction_{};
};

#undef ADV_SAVE_CONSTEXPR

} }
