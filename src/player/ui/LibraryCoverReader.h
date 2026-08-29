#pragma once
#include <M5GFX.h>
#include "media/ResourceIo.h"
#include "media/ImageBand.h"
namespace adv_walkman { namespace player {
class LibraryCoverReader {
 public:
    void select(const char* directory,uint32_t generation);
    void suspend(){band_.cancel();file_.close();if(state_==MediaState::Loading)phase_=1;}
    void cancelPending(){band_.cancel();file_.close();bandGeneration_=0;if(state_==MediaState::Loading){phase_=0;state_=MediaState::Idle;}}
    void release(){cancelPending();phase_=0;state_=MediaState::Idle;path_[0]=0;generation_=0;}
    bool bandActive()const{return band_.active();}
    bool prepareBand(lgfx::LGFXBase& row,int y,int h,uint32_t generation){
        if(generation!=generation_){++staleRejects;return false;}
        if(!band_.active())bandGeneration_=generation;
        return bandGeneration_==generation&&band_.begin(row,y,h,0,width_,height_)&&band_.ready();
    }
    bool bandReady()const{return band_.ready();}
    bool finishBand(uint32_t generation){
        if(!band_.active())return true;
        if(generation!=bandGeneration_){++staleRejects;return false;}
        band_.cancel();bandGeneration_=0;return true;
    }
    void finishBand(){band_.cancel();bandGeneration_=0;}
    void service();
    MediaState state()const{return state_;}
    const char* path()const{return path_;}
    uint32_t generation()const{return generation_;}
    const ResourceFailure& failure()const{return failure_;}
    uint32_t reads=0,errors=0,cancels=0,serviceMaxUs=0,validationHits=0,staleRejects=0;
 private:
    void fail(const char* reason);
    static uint64_t pathHash(const char* path);
    bool restoreValidation(uint64_t key);
    void rememberValidation(uint64_t key);
    fs::File file_;char path_[600]{};uint8_t bytes_[512]{};
    ImageBand band_;
    uint16_t width_=120,height_=120;
    ResourceFailure failure_{};MediaState state_=MediaState::Idle;
    uint8_t phase_=0;uint32_t crc_=0,expected_=0,remaining_=0,fileBytes_=0;
    uint32_t generation_=0,bandGeneration_=0;
    struct Validation {uint64_t key=0;uint32_t expected=0,fileBytes=0;uint16_t width=0,height=0;bool valid=false;} validations_[3];
    uint8_t nextValidation_=0;
};
} }
