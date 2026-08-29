#pragma once

#include <cstdint>

namespace adv_walkman { namespace player {

#if __cplusplus >= 201402L
#define ADV_UI_TX_CONSTEXPR constexpr
#else
#define ADV_UI_TX_CONSTEXPR inline
#endif

// A result from one bounded UI step.  Storage and display owners return this
// instead of encoding progress in unrelated booleans.
enum class UiWorkResult : uint8_t {
    Idle,
    Progress,
    IoCompleted,
    ReadyToPresent,
    Presented,
    Cancelled,
    Failed,
};

struct UiRequestToken {
    uint32_t pageEpoch=0;
    uint32_t requestGeneration=0;
    uint32_t resourceGeneration=0;
    constexpr UiRequestToken()=default;
    constexpr UiRequestToken(uint32_t page,uint32_t request,uint32_t resource):
        pageEpoch(page),requestGeneration(request),resourceGeneration(resource){}
    constexpr bool operator==(const UiRequestToken& other)const{
        return pageEpoch==other.pageEpoch &&
               requestGeneration==other.requestGeneration &&
               resourceGeneration==other.resourceGeneration;
    }
    constexpr bool operator!=(const UiRequestToken& other)const{return !(*this==other);}
};

enum class LibraryPageState : uint8_t {
    Idle,
    RetainingPreviousFrame,
    PreparingModel,
    ValidatingCover,
    Rendering,
    Presented,
    Recovering,
    Error,
};

enum LibraryFrameRegion : uint8_t {
    LibraryNameRegion=1,
    LibraryWheelRegion=2,
};

// Library has disjoint regions, so the Player's contiguous FrameCommit is not
// sufficient.  This contract still requires contiguous cover rows and exact
// transaction ownership before a visible target can be published.
struct LibraryFrameCommit {
    UiRequestToken token{};
    uint16_t coverNext=0;
    uint8_t regions=0;
    bool active=false;
    ADV_UI_TX_CONSTEXPR void begin(UiRequestToken value){
        token=value;coverNext=0;regions=0;active=true;
    }
    ADV_UI_TX_CONSTEXPR bool mark(UiRequestToken value,uint8_t region){
        if(!active||value!=token)return false;
        regions|=region;return true;
    }
    ADV_UI_TX_CONSTEXPR bool submitCover(UiRequestToken value,unsigned y,unsigned height){
        if(!active||value!=token||y!=coverNext||!height||height>18||y+height>174)return false;
        coverNext+=height;return true;
    }
    constexpr bool complete()const{
        return active && coverNext==174 &&
               (regions&(LibraryNameRegion|LibraryWheelRegion))==
                   (LibraryNameRegion|LibraryWheelRegion);
    }
    ADV_UI_TX_CONSTEXPR void cancel(){active=false;}
};

class LibraryTransaction final {
 public:
    ADV_UI_TX_CONSTEXPR void request(UiRequestToken value,uint32_t now){
        requested_=value;state_=LibraryPageState::RetainingPreviousFrame;
        progressAt_=now;recoveryUsed_=false;commit_.begin(value);
    }
    ADV_UI_TX_CONSTEXPR void preparing(uint32_t now){state_=LibraryPageState::PreparingModel;progressAt_=now;}
    ADV_UI_TX_CONSTEXPR void validating(uint32_t now){state_=LibraryPageState::ValidatingCover;progressAt_=now;}
    ADV_UI_TX_CONSTEXPR void rendering(uint32_t now){state_=LibraryPageState::Rendering;progressAt_=now;}
    ADV_UI_TX_CONSTEXPR void progress(uint32_t now){progressAt_=now;}
    ADV_UI_TX_CONSTEXPR bool mark(uint8_t region,uint32_t now){
        const bool accepted=commit_.mark(requested_,region);if(accepted)progressAt_=now;return accepted;
    }
    ADV_UI_TX_CONSTEXPR bool submitCover(unsigned y,unsigned height,uint32_t now){
        const bool accepted=commit_.submitCover(requested_,y,height);if(accepted)progressAt_=now;return accepted;
    }
    ADV_UI_TX_CONSTEXPR bool publish(uint32_t now){
        if(!commit_.complete())return false;
        displayed_=requested_;state_=LibraryPageState::Presented;progressAt_=now;return true;
    }
    ADV_UI_TX_CONSTEXPR void cancel(uint32_t now){commit_.cancel();state_=LibraryPageState::Idle;progressAt_=now;}
    constexpr bool stalled(uint32_t now)const{
        return state_!=LibraryPageState::Idle&&state_!=LibraryPageState::Presented&&
               state_!=LibraryPageState::Error&&uint32_t(now-progressAt_)>=2000;
    }
    ADV_UI_TX_CONSTEXPR bool beginRecovery(uint32_t now){
        commit_.cancel();progressAt_=now;
        if(recoveryUsed_){state_=LibraryPageState::Error;return false;}
        recoveryUsed_=true;state_=LibraryPageState::Recovering;return true;
    }
    ADV_UI_TX_CONSTEXPR void fail(uint32_t now){commit_.cancel();state_=LibraryPageState::Error;progressAt_=now;}
    constexpr const UiRequestToken& requested()const{return requested_;}
    constexpr const UiRequestToken& displayed()const{return displayed_;}
    constexpr LibraryPageState state()const{return state_;}
    constexpr const LibraryFrameCommit& commit()const{return commit_;}
    constexpr uint32_t progressAt()const{return progressAt_;}
    constexpr bool recoveryUsed()const{return recoveryUsed_;}
 private:
    UiRequestToken requested_{},displayed_{};
    LibraryFrameCommit commit_{};
    LibraryPageState state_=LibraryPageState::Idle;
    uint32_t progressAt_=0;
    bool recoveryUsed_=false;
};

#undef ADV_UI_TX_CONSTEXPR

} }
