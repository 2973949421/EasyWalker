#pragma once
#include <M5GFX.h>
#include "media/ResourceIo.h"
#include "media/ImageBand.h"
namespace adv_walkman { namespace player {
class LibraryCoverReader {
 public:
    void select(const char* directory);
    void suspend(){band_.cancel();file_.close();if(state_==MediaState::Loading)phase_=1;}
    void release(){band_.cancel();file_.close();phase_=0;state_=MediaState::Idle;row_=requested_=-1;path_[0]=0;}
    bool bandActive()const{return band_.active();}
    bool prepareBand(lgfx::LGFXBase& row,int y,int h){band_.begin(row,y,h,0,width_,height_);return band_.ready();}
    void finishBand(){band_.cancel();}
    void service();
    void requestRow(int y){requested_=y;}
    bool rowReady(int y)const{return state_==MediaState::Ready&&row_==y;}
    void drawRow(lgfx::LGFXBase& canvas,int y)const;
    MediaState state()const{return state_;}
    const char* path()const{return path_;}
    const ResourceFailure& failure()const{return failure_;}
    uint32_t reads=0,errors=0,cancels=0,serviceMaxUs=0;
 private:
    void fail(const char* reason);
    fs::File file_;char path_[600]{};uint8_t bytes_[512]{};
    ImageBand band_;
    uint16_t width_=120,height_=120;
    ResourceFailure failure_{};MediaState state_=MediaState::Idle;
    uint8_t phase_=0;uint32_t crc_=0,expected_=0,remaining_=0;
    int row_=-1,requested_=-1;
};
} }
