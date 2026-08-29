#pragma once

#include "UiTransaction.h"

namespace adv_walkman { namespace player {

#if __cplusplus >= 201402L
#define ADV_LIBRARY_PAGE_CONSTEXPR constexpr
#else
#define ADV_LIBRARY_PAGE_CONSTEXPR inline
#endif

enum class LibraryRecoveryAction : uint8_t { None, Rebuild, Error };

// Owns every generation and recovery decision for the Library page.  The
// coordinator may route actions, but it cannot manufacture a partially
// matching token or infer recovery from unrelated browser/band booleans.
class LibraryPageController final {
 public:
    constexpr UiRequestToken token()const{return {pageEpoch_,requestGeneration_,resourceGeneration_};}
    ADV_LIBRARY_PAGE_CONSTEXPR void pageChanged(){bump(pageEpoch_);bump(requestGeneration_);bump(resourceGeneration_);resetRecovery();}
    ADV_LIBRARY_PAGE_CONSTEXPR void requestSelection(){bump(requestGeneration_);bump(resourceGeneration_);resetRecovery();}
    ADV_LIBRARY_PAGE_CONSTEXPR void resourceChanged(){bump(requestGeneration_);bump(resourceGeneration_);resetRecovery();}
    ADV_LIBRARY_PAGE_CONSTEXPR LibraryRecoveryAction checkProgress(const LibraryTransaction& transaction,uint32_t now){
        if(!transaction.stalled(now))return LibraryRecoveryAction::None;
        const uint32_t request=transaction.requested().requestGeneration;
        if(recoveryRequest_!=request){recoveryRequest_=request;recoveryCount_=0;}
        if(!recoveryCount_++){bump(resourceGeneration_);return LibraryRecoveryAction::Rebuild;}
        return LibraryRecoveryAction::Error;
    }
    uint8_t recoveryCount()const{return recoveryCount_;}
 private:
    ADV_LIBRARY_PAGE_CONSTEXPR static void bump(uint32_t& value){if(!++value)++value;}
    ADV_LIBRARY_PAGE_CONSTEXPR void resetRecovery(){recoveryRequest_=requestGeneration_;recoveryCount_=0;}
    uint32_t pageEpoch_=1,requestGeneration_=1,resourceGeneration_=1,recoveryRequest_=0;
    uint8_t recoveryCount_=0;
};

#undef ADV_LIBRARY_PAGE_CONSTEXPR

} }
