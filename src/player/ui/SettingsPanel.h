#pragma once
#include "DisplaySettingsStore.h"
#include "UiTypes.h"
#include "media/FontCache.h"
#include "player/app/PlayerRuntime.h"
#include <esp_partition.h>
namespace adv_walkman { namespace player {
class SettingsPanel {
 public:
    void begin(){store.begin();}
    void open(){panel_=0;selected_=0;confirmed_=false;invalidate();}
    bool handle(UiAction action,PlayerRuntime& player); // false only for root Esc
    void service(PlayerRuntime& player,bool logIdle);
    bool prepare(FontCache& fonts);
    bool render(M5GFX& display,M5Canvas& row,FontCache& fonts);
    void invalidate(){stripe_=0;}
    bool complete()const{return stripe_>=240;}
    bool returning()const{return returnState_==1||returnState_==2;}
    bool readyToReturn()const{return returnState_==2;}
    void finishReturn(bool logOk);
    DisplaySettingsStore store;
    uint32_t changes=0,frames=0,returnRequests=0,returnErrors=0;
    const char* returnError()const{return returnError_;}
 private:
    void failReturn(const char* error);
    uint8_t panel_=0,selected_=0,returnState_=0;
    bool confirmed_=false;
    int stripe_=0;
    uint32_t returnAt_=0,lastWrites_=0,lastErrors_=0;
    const esp_partition_t* target_=nullptr;
    const char* returnError_="none";
};
} }
