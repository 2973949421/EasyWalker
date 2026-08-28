#include "DisplaySettingsStore.h"
#include "media/MediaTypes.h"
#include <cstring>
#include <algorithm>
namespace adv_walkman { namespace player {
namespace {
const char* paths[]={"/ADVWalkman/state/display-a.bin","/ADVWalkman/state/display-b.bin"};
void put32(uint8_t* p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=v>>(i*8);}
}
bool DisplaySettingsStore::decode(const uint8_t* b,DisplayPreferences& out,uint32_t& gen)const{
    if(!validDisplayRecord(b))return false;
    out.brightness=b[16];out.playerTimeout=b[17];out.otherTimeout=b[18];gen=mediaU32(b+8);
    return validDisplayPreferences(out)&&b[19]==0;
}
void DisplaySettingsStore::begin(){
    for(uint8_t i=0;i<2;++i){auto f=SD.open(paths[i],"r");if(!f){if(SD.exists(paths[i]))++openFailures;continue;}
        const bool ok=f.size()==24&&f.read(bytes_,24)==24;f.close();DisplayPreferences p;uint32_t g=0;
        if(!ok||!decode(bytes_,p,g)){++invalidSlots;continue;}
        if(!loaded||newerDisplayGeneration(g,generation_)){value=p;generation_=g;slot_=i;loaded=true;}}
    restored=value;
}
void DisplaySettingsStore::fail(const char* reason){phase_=file_?6:0;dirty_=false;error_=reason;++errors;}
void DisplaySettingsStore::service(uint32_t now,bool storageIdle){
    if(!storageIdle)return;
    const uint32_t started=micros();
    if(phase_==0 && dirty_ && uint32_t(now-changedAt_)>=1000){
        std::memset(bytes_,0,24);std::memcpy(bytes_,"DSPL",4);bytes_[4]=1;bytes_[6]=24;
        put32(bytes_+8,generation_+1);put32(bytes_+12,4);bytes_[16]=value.brightness;bytes_[17]=value.playerTimeout;bytes_[18]=value.otherTimeout;
        put32(bytes_+20,mediaCrc(~0U,bytes_,20)^~0U);writingRevision_=revision_;
        file_=SD.open(paths[slot_^1],"w");if(!file_)fail("settings_open");else phase_=1;
    }else if(phase_==1){if(file_.write(bytes_,24)!=24)fail("settings_write");else phase_=2;
    }else if(phase_==2){file_.flush();if(file_.getWriteError())fail("settings_flush");else phase_=5;
    }else if(phase_==5){file_.close();phase_=3;
    }else if(phase_==6){file_.close();phase_=0;
    }else if(phase_==3){file_=SD.open(paths[slot_^1],"r");if(!file_)fail("settings_verify_open");else phase_=4;
    }else if(phase_==4){uint8_t check[24];DisplayPreferences p;uint32_t g;
        const bool ok=file_.size()==24&&file_.read(check,24)==24&&decode(check,p,g)&&!std::memcmp(check,bytes_,24);
        if(!ok)fail("settings_verify");else phase_=7;
    }else if(phase_==7){file_.close();generation_=mediaU32(bytes_+8);slot_^=1;phase_=0;dirty_=revision_!=writingRevision_;++writes;error_="none";}
    serviceMaxUs=std::max<uint32_t>(serviceMaxUs,micros()-started);
}
} }
