#pragma once
#include <SD.h>
#include "DisplayPolicy.h"
namespace adv_walkman { namespace player {
class DisplaySettingsStore {
 public:
    void begin();
    void changed(uint32_t now){dirty_=true;changedAt_=now;++revision_;error_="none";}
    void saveSoon(uint32_t now){if(dirty_)changedAt_=now-1000;}
    void service(uint32_t now,bool storageIdle);
    bool idle()const{return !phase_&&!dirty_;}
    bool writing()const{return phase_!=0;}
    DisplayPreferences value,restored;
    uint32_t writes=0,errors=0,serviceMaxUs=0,invalidSlots=0,openFailures=0;
    bool loaded=false;
    const char* error()const{return error_;}
 private:
    bool decode(const uint8_t* bytes,DisplayPreferences& output,uint32_t& generation)const;
    void fail(const char* reason);
    fs::File file_;
    uint8_t bytes_[24]{};
    uint32_t generation_=0,changedAt_=0,revision_=0,writingRevision_=0;
    uint8_t slot_=0,phase_=0;
    bool dirty_=false;
    const char* error_="none";
};
} }
